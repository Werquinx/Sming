/****
 * Sming Framework Project - Open Source framework for high efficiency native ESP8266 development.
 * Created 2015 by Skurydin Alexey
 * http://github.com/anakod/Sming
 * All files of the Sming Core are provided under the LGPL v3 license.
 ****/

#include "../SmingCore/DataSourceStream.h"
#include "../SmingCore/Network/TcpConnection.h"
#include "../SmingCore/Network/HttpRequest.h"
#include "WiringFrameworkDependencies.h"

MemoryDataStream::MemoryDataStream()
{
	buf = NULL;
	pos = NULL;
	size = 0;
	capacity = 0;
}

MemoryDataStream::~MemoryDataStream()
{
	delete[] buf;
	buf = NULL;
	pos = NULL;
	size = 0;
}

size_t MemoryDataStream::write(uint8_t charToWrite)
{
	return write(&charToWrite, 1);
}

size_t MemoryDataStream::write(const uint8_t* data, size_t len)
{
	//TODO: add queued buffers without full copy
	if (buf == NULL)
	{
		buf = new char[len + 1];
		buf[len] = '\0';
		os_memcpy(buf, data, len);
	}
	else
	{
		int cur = size;
		int required = cur + len + 1;
		if (required > capacity)
		{
			capacity = required < 256 ? required + 128 : required + 64;
			char* nbuf = new char[capacity];
			buf[cur + len] = '\0';
			os_memcpy(nbuf, buf, cur);
			os_memcpy(nbuf + cur, data, len);
			delete[] buf;
			buf = nbuf;
		}
		else
		{
			buf[cur + len] = '\0';
			os_memcpy(buf + cur, data, len);
		}
	}
	pos = buf;
	size += len;
	return len;
}

uint16_t MemoryDataStream::getDataPointer(char** data)
{
	*data = pos;
	int available = size - (pos - buf);
	return available;
}

bool MemoryDataStream::seek(int len)
{
	if (len < 0) return false;

	pos += len;
	return true;
}

bool MemoryDataStream::isFinished()
{
	return size == (pos - buf);
}

///////////////////////////////////////////////////////////////////////////

FileStream::FileStream(String fileName)
{
	handle = fileOpen(fileName.c_str(), eFO_ReadOnly);
	if (handle == -1)
	{
		debugf("File wasn't found: %s", fileName.c_str());
		buffer = NULL;
		size = -1;
		pos = 0;
	}

	// Get size
	fileSeek(handle, 0, eSO_FileEnd);
	size = fileTell(handle);

	fileSeek(handle, 0, eSO_FileStart);
	pos = 0;
	buffer = new char[min(size, NETWORK_SEND_BUFFER_SIZE)];

	debugf("send file: %s (%d bytes)", fileName.c_str(), size);
}

FileStream::~FileStream()
{
	fileClose(handle);
	handle = 0;
	pos = 0;
	if (buffer != NULL)
		delete[] buffer;
	buffer = NULL;
}

uint16_t FileStream::getDataPointer(char** data)
{
	int len = min(NETWORK_SEND_BUFFER_SIZE, size - pos);
	int available = fileRead(handle, buffer, len);
	fileSeek(handle, pos, eSO_FileStart); // Don't move cursor now
	*data = buffer;
	return available;
}

bool FileStream::seek(int len)
{
	if (len < 0) return false;

	bool result = fileSeek(handle, len, eSO_CurrentPos) == 0;
	if (result) pos += len;
	return result;
}

bool FileStream::isFinished()
{
	return fileIsEOF(handle);
}

String FileStream::fileName()
{
	spiffs_stat stat;
	fileStats(handle, &stat);
	return String((char*)stat.name);
}

bool FileStream::fileExist()
{
	return size != -1;
}

///////////////////////////////////////////////////////////////////////////

TemplateFileStream::TemplateFileStream(String templateFileName)
	: FileStream(templateFileName)
{
	state = eTES_Wait;
}

TemplateFileStream::~TemplateFileStream()
{
}

uint16_t TemplateFileStream::getDataPointer(char** data)
{
	debugf("READ Template (%d)", state);

	if (state == eTES_StartVar)
	{
		if (templateData.contains(varName))
		{
			// Return variable value
			debugf("StartVar %s", varName.c_str());
			*data = (char*)templateData[varName].c_str();
			seek(skipBlockSize);
			varDataPos = 0;
			state = eTES_SendingVar;
			return templateData[varName].length();
		}
		else
		{
			debugf("var %s not found", varName.c_str());
			state = eTES_Wait;
			int len = FileStream::getDataPointer(data);
			return min(len, skipBlockSize);
		}
	}
	else if (state == eTES_SendingVar)
	{
		String *val = &templateData[varName];
		if (varDataPos < val->length())
		{
			debugf("continue TRANSFER variable value (not completed)");
			*data = (char*)val->c_str();
			*data += varDataPos;
			return val->length() - varDataPos;
		}
		else
		{
			debugf("continue to plaint text");
			state = eTES_Wait;
		}
	}

	int len = FileStream::getDataPointer(data);
	char* tpl = *data;
	if (tpl && len > 0)
	{
		char* end = tpl + len;
		char* cur = (char*)memchr(tpl, '{', len);
		char* lastFound = cur;
		while (cur != NULL)
		{
			lastFound = cur;
			char* p = cur + 1;
			for (; p < end; p++)
			{
				if (isspace(*p))
					break; // Not a var name
				else if (p - cur > TEMPLATE_MAX_VAR_NAME_LEN)
					break; // To long for var name
				else if (*p == '{')
					break; // New start..

				if (*p == '}')
				{
					int block = p - cur + 1;
					char varname[TEMPLATE_MAX_VAR_NAME_LEN + 1] = {0};
					os_memcpy(varname, cur + 1, p - cur - 1); // name without { and }
					varName = varname;
					state = eTES_Found;
					varWaitSize = cur - tpl;
					debugf("found var: %s, at %d (%d) - %d, send size %d", varName.c_str(), cur - tpl + 1, cur - tpl + getPos(), p - tpl, cur - tpl);
					skipBlockSize = block;
					return cur - tpl; // return only plain text from template without our variable
				}
			}
			cur = (char*)memchr(p, '{', len - (p - tpl)); // continue searching..
		}
		if (lastFound != NULL && (lastFound - tpl) > (len - TEMPLATE_MAX_VAR_NAME_LEN))
		{
			debugf("trim end to %d from %d", lastFound - tpl, len);
			len = lastFound - tpl; // It can be a incomplete variable name. Don't split it!
		}
	}

	debugf("plain template text pos: %d, len: %d", getPos(), len);
	return len;
}

bool TemplateFileStream::seek(int len)
{
	if (len < 0) return false;
	//debugf("SEEK: %d, (%d)", len, state);

	if (state == eTES_Found)
	{
		//debugf("SEEK before Var: %d, (%d)", len, varWaitSize);
		varWaitSize -= len;
		if (varWaitSize == 0) state = eTES_StartVar;
	}
	else if (state == eTES_SendingVar)
	{
		varDataPos += len;
		return false; // not the end
	}

	return FileStream::seek(len);
}

void TemplateFileStream::setVar(String name, String value)
{
	templateData[name] = value;
}

void TemplateFileStream::setVarsFromRequest(const HttpRequest& request)
{
	if (request.requestGetParameters != NULL)
		templateData.setMultiple(*request.requestGetParameters);
	if (request.requestPostParameters != NULL)
		templateData.setMultiple(*request.requestPostParameters);
}

///////////////////////////////////////////////////////////////////////////

JsonObjectStream::JsonObjectStream()
	: rootNode(buffer.createObject()), send(true)
{
}

JsonObjectStream::~JsonObjectStream()
{
}

JsonObject& JsonObjectStream::getRoot()
{
	return rootNode;
}

uint16_t JsonObjectStream::getDataPointer(char** data)
{
	if (rootNode != JsonObject::invalid() && send)
	{
		int len = rootNode.prettyPrintTo(*this);
		send = false;
	}

	return MemoryDataStream::getDataPointer(data);
}
