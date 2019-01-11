#include <curl/curl.h>
#include "curl-data.h"
#include <fstream>
#include <iostream>
#include <codecvt>

using namespace std;

size_t CurlData::WriteBodyCallback(char *ptr, size_t size, size_t nmemb, std::string &str)
{
	size_t total = size * nmemb;
	if (total)
		str.append(ptr, total);

	return total;
}

size_t CurlData::WriteHeaderCallback(char *ptr, size_t size, size_t nmemb, std::string &str)
{
	size_t total = size * nmemb;
	if (total)
		str.append(ptr, total);

	return total;
}

size_t CurlData::ReadCallback(void *ptr, size_t size, size_t nmemb, void *userp)
{
	PDATA_BUFFER databuf = (PDATA_BUFFER)userp;
	if (databuf == nullptr) {
		return 0;
	}
	size_t tocopy = size * nmemb;

	if (tocopy < 1 || !databuf->data_size) {
		//sleep(10000);
		return 0;
	}

	if (tocopy > databuf->data_size) {
		tocopy = databuf->data_size;
	}

	memcpy(ptr, databuf->readptr, tocopy);
	databuf->readptr += tocopy;
	databuf->data_size -= tocopy;
	return tocopy;
}

bool CurlData::post(std::string &sheader, std::string &sbody, std::string &err)
{
    bool bRet = false;
	char error[CURL_ERROR_SIZE];
	CURLcode code;

	string versionString("User-Agent: zdx_upgrade ");
	versionString += "1.0.0.0";

	string contentTypeString;
	if (!content_type.empty()) {
		contentTypeString += "Content-Type: ";
		contentTypeString += content_type;
	}

	auto curl_deleter = [] (CURL *curl) {curl_easy_cleanup(curl);};
	using Curl = unique_ptr<CURL, decltype(curl_deleter)>;

	Curl curl{curl_easy_init(), curl_deleter};
	if (curl) {
		struct curl_slist *header = nullptr;
		string sHeader, sBody;

		header = curl_slist_append(header,
				versionString.c_str());

		if (!contentTypeString.empty()) {
			header = curl_slist_append(header,
					contentTypeString.c_str());
		}

		header = curl_slist_append(header, "Expect:");  // 解决有些服务器需要 100 continue问题

		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2);
		curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_HEADER, 0L);  // 0 sBody只包含body体，1 sBody 包含html的header+body
		curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header);
		curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteBodyCallback);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &sBody);
		curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
		curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &sHeader);

#ifdef CURL_DOES_CONVERSIONS
		curl_easy_setopt(curl.get(), CURLOPT_TRANSFERTEXT, 1L);
#endif
#if LIBCURL_VERSION_NUM >= 0x072400
		curl_easy_setopt(curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0L);
#endif
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, (long)data_buffer.data_size);
		curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, ReadCallback);
		curl_easy_setopt(curl.get(), CURLOPT_READDATA, &data_buffer);
		curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 0L);
		curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L); // 支持跳转
		//curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);  // sBody不接受内容
		
		code = curl_easy_perform(curl.get());
		if (code != CURLE_OK) {
            sheader = "";
            sbody = "";
            err = error;
            bRet = false;
		} else {
            sheader = sHeader;
            sbody = sBody;
            err = error;
            // 输出显示
            //wstring_convert<codecvt_utf8<wchar_t>> utf8_cvt;
            //wstring_convert<codecvt<wchar_t, char, mbstate_t>> gbk_cvt(new codecvt<wchar_t, char, mbstate_t>("chs"));
            //wstring t = utf8_cvt.from_bytes(sBody);
            bRet = true;
		}

		curl_slist_free_all(header);

		if (data_buffer.delptr) {
			delete[] data_buffer.delptr;
			data_buffer.delptr = nullptr;
		}
	}
    return bRet;
}

bool CurlData::PrepareData(const std::string& name, const std::string& data)
{
	string str("");
	char* pbuf = nullptr;
	size_t dst_len, len;

	// 对数据 Encode
	len = data.length();
	dst_len = 3 * len + 1;
	pbuf = (char*)malloc(dst_len);
	memset(pbuf, 0, dst_len);
	MyUrlEncode(pbuf, dst_len, data.c_str(), len);
	str += name;
	str += "=";
	str += pbuf;
	free(pbuf);

	// 拼接原始数据
	if (data_buffer.readptr != nullptr) {
		str = "&" + str;
	}
	size_t bufsize = data_buffer.data_size + str.length();

	pbuf = new char[bufsize + 1];
	memset(pbuf, 0, sizeof(char)*(bufsize + 1));
	if (data_buffer.readptr) {
		memcpy(pbuf, data_buffer.readptr, data_buffer.data_size);
		delete[] data_buffer.readptr;
		data_buffer.readptr = nullptr;
	}
	memcpy(pbuf + data_buffer.data_size, str.c_str(), str.length());

	data_buffer.readptr = pbuf;
	data_buffer.delptr = data_buffer.readptr;
	data_buffer.data_size = bufsize;

	return true;
}

bool CurlData::PrepareData(const std::string& name, void* data, size_t data_size)
{
	if ((!data) || (data_size <= 0)) {
		return false;
	}

	string str("");
	char* pbuf = nullptr;
	size_t dst_len, len;

	// 对数据 Encode
	len = data_size;
	dst_len = 3 * len + 1;
	pbuf = new char[dst_len];
	memset(pbuf, 0, dst_len);
	MyUrlEncode(pbuf, dst_len, data, len);
	str += name;
	str += "=";
	str += pbuf;
	delete[] pbuf;
	pbuf = nullptr;

	// 拼接原始数据
	if (data_buffer.readptr != nullptr) {
		str = "&" + str;
	}
	size_t bufsize = data_buffer.data_size + str.length();

	pbuf = new char[bufsize + 1];
	memset(pbuf, 0, sizeof(char)*(bufsize + 1));
	if (data_buffer.readptr) {
		memcpy(pbuf, data_buffer.readptr, data_buffer.data_size);
		delete[] data_buffer.readptr;
		data_buffer.readptr = nullptr;
	}

	memcpy(pbuf + data_buffer.data_size, str.c_str(), str.length());

	data_buffer.readptr = pbuf;
	data_buffer.delptr = data_buffer.readptr;
	data_buffer.data_size = bufsize;

	return true;
}

bool CurlData::PrepareDataFromFile(const std::string& name, const std::string& filename)
{
	fstream file;
	size_t filesize = 0;
	char* pfilebuf = nullptr;

	bool exists = os_file_exists(filename.c_str());
	if (!exists) {
		return false;
	}

	file.open(filename.c_str(), ios_base::in | ios_base::binary);
	if (!file.is_open()) {
        std::cout << "\n读文件[" << filename << "打开失败！\n";
		file.close();
		return false;
	}
	else {
		file.seekg(0, ios_base::end);
		filesize = (size_t)file.tellg();
		file.seekg(0, ios_base::beg);
		pfilebuf = new char[filesize + 1];
		memset(pfilebuf, 0, filesize + 1);
		file.read(pfilebuf, filesize);
	}
	file.close();

	string str = name + "=";
	if (data_buffer.readptr) {
		str = "&" + str;
	}

	size_t bufsize = str.length() + data_buffer.data_size + 3 * filesize;
	char* pbuf = new char[bufsize + 1];
	memset(pbuf, 0, sizeof(char)*(bufsize + 1));
	if (data_buffer.readptr) {
		memcpy(pbuf, data_buffer.readptr, data_buffer.data_size);
		delete data_buffer.readptr;
		data_buffer.readptr = nullptr;
	}

	data_buffer.delptr = pbuf;
	data_buffer.readptr = pbuf;

	char* ptr = pbuf + data_buffer.data_size;
	memcpy(ptr, str.c_str(), str.length());
	data_buffer.data_size += str.length();

	ptr = pbuf + data_buffer.data_size;
	bufsize = 3 * filesize + 1;
	MyUrlEncode(ptr, bufsize, pfilebuf, filesize);
	data_buffer.data_size += bufsize;
	
	delete [] pfilebuf;
	pfilebuf = nullptr;
	
	return true;
}

size_t CurlData::MyUrlEncode(void* dst, size_t& dst_len, const void* src, size_t len)
{
	if (dst == NULL || src == NULL || len <= 0) {
		return 0;
	}

	char* psrc = (char*)src;
	char* pdst = (char*)dst;
	memset(dst, 0, dst_len);
	size_t i = 0;
	size_t j = 0;
	for (i = 0; i < len; i++) {
		if (psrc[i] == ' ') {
			pdst[j++] = '%';
			pdst[j++] = '2';
			pdst[j++] = '0';
		}
		else if (psrc[i] == '#') {
			pdst[j++] = '%';
			pdst[j++] = '2';
			pdst[j++] = '3';
		}
		else if (psrc[i] == '%') {
			pdst[j++] = '%';
			pdst[j++] = '2';
			pdst[j++] = '5';
		}
		else if (psrc[i] == '&') {
			pdst[j++] = '%';
			pdst[j++] = '2';
			pdst[j++] = '6';
		}
		else if (psrc[i] == '+') {
			pdst[j++] = '%';
			pdst[j++] = '2';
			pdst[j++] = 'B';
		}
		//else if (psrc[i] == '.') {
		//	pdst[j++] = '%';
		//	pdst[j++] = '2';
		//	pdst[j++] = 'E';
		//}
		else if (psrc[i] == '/') {
			pdst[j++] = '%';
			pdst[j++] = '2';
			pdst[j++] = 'F';
		}
		//else if (psrc[i] == ':') {
		//	pdst[j++] = '%';
		//	pdst[j++] = '3';
		//	pdst[j++] = 'A';
		//}
		else if (psrc[i] == '=') {
			pdst[j++] = '%';
			pdst[j++] = '3';
			pdst[j++] = 'D';
		}
		else if (psrc[i] == '?') {
			pdst[j++] = '%';
			pdst[j++] = '3';
			pdst[j++] = 'F';
		}
		//else if (psrc[i] == '\\') {
		//	pdst[j++] = '%';
		//	pdst[j++] = '5';
		//	pdst[j++] = 'C';
		//}
		else {
			pdst[j++] = psrc[i];
		}
	}

	dst_len = j;

	return j;
}

bool CurlData::os_file_exists(const std::string &filename)
{
    std::ifstream fin(filename, std::ios::in);
    if (fin.good()) {
        fin.close();
        return true;
    }
    return false;
}
