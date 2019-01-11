#include <curl/curl.h>
#include "curl-download.h"
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>


using namespace std;
#pragma warning(disable: 4996)

//#pragma comment(lib, "libcurl.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "crypt32.lib") // ѡ��ssl������Ҫ��lib

size_t CurlDownload::WriteBodyCallback(char *ptr, size_t size, size_t nmemb, std::string &str)
{
	size_t total = size * nmemb;
	if (total)
		str.append(ptr, total);

	return total;
}

size_t CurlDownload::WriteHeaderCallback(char *ptr, size_t size, size_t nmemb, std::string &str)
{
	size_t total = size * nmemb;
	if (total)
		str.append(ptr, total);

	return total;
}

size_t CurlDownload::WriteJsonData(void *ptr, size_t size, size_t nmemb, void *stream)
{
    string data((const char*)ptr, (size_t)size * nmemb);

    *((stringstream*)stream) << data << endl;

    return size * nmemb;
}

size_t CurlDownload::WriteDownloadData(void* buffer, size_t size, size_t nmemb, void *stream)
{
    size_t block_size = 0;
    CurlDownload* pDownloader = (CurlDownload*)stream;
    if (pDownloader && pDownloader->GetFileHandle()) {
        pDownloader->m_download_status = 1;
        block_size = fwrite(buffer, size, nmemb, pDownloader->GetFileHandle());
    }
    return block_size;
}

size_t CurlDownload::DownloadProgress(void *buffer, double dltotal, double dlnow, double ultotal, double ulnow)
{
    CurlDownload* pDownloader = (CurlDownload*)buffer;
    if (pDownloader && (int(dltotal) > 0)) {
        double ztotol = pDownloader->m_download_file_size + dltotal;
        double znow = pDownloader->m_download_file_size + dlnow;
        if ((long long)znow == pDownloader->m_max_file_size) {
            pDownloader->m_download_status = 2;
        }

        pDownloader->m_current_progress = (int)((znow / ztotol) * 100);
        // ֪ͨ�������������ؽ���  
        std::cout << "�ļ���С:" << (long long)ztotol / 1024 << "KB,������:" << (long long)znow / 1024 << "KB,���ؽ���: " << pDownloader->m_current_progress << " %" << std::endl;
        
        //// test �ϵ�����
        //static int n = 0;
        //n++;
        //if (n == 2000) {
        //    n = 0;
        //    pDownloader->SetDownLoadStop();
        //}

        // ���ط�0ֵ����
        if (pDownloader->GetDownLoadCancel()) {

            std::cout << "���ر�����ֹͣ�ˣ���" << std::endl;
            //// test �ϵ�����
            //if (n == 0) {
            //    pDownloader->m_download_cancel = false;
            //}
            return -1;
        }
    }
    return 0;
}

FILE *CurlDownload::GetFileHandle()
{
    return m_file;
}

std::string CurlDownload::GetLocalFileName()
{
    return m_local_filename;
}

bool CurlDownload::GetDownLoadCancel()
{
    return m_download_cancel;
}

void CurlDownload::SetDownLoadStop()
{
    m_download_cancel = true;
}

long long CurlDownload::GetLocalFileLenth(void* stream)
{
    long long filesize = 0;
    CurlDownload* pDownloader = (CurlDownload*)stream;
    if (pDownloader) {
        filesize = pDownloader->GetFileSize(pDownloader->GetLocalFileName());
    }
    return filesize;
}

long long CurlDownload::GetFileSize(const std::string &filename)
{
    long long filesize = 0;
    FILE* file;
    file = fopen(filename.c_str(), "rb");
    if (file == NULL)
        perror("Error opening file");
    else
    {
        fseek(file, 0, SEEK_END);
        filesize = ftell(file);
        fclose(file);
    }
    return filesize;
}

double CurlDownload::GetDownloadFileLenth(const char *url) {
    double downloadFileLenth = 0;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    // ��������nobodyѡ���ǣ�����������û�д�ӡ���
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    //curl_easy_setopt(curl, CURLOPT_HEADER, 1L);   // �������ѡ����������У����д�ӡ���
    //curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);  // ����������ѡ���ǣ����������У�û�д�ӡ���
    //curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");  // �������ѡ���ʱ�����������У�û�д�ӡ���
    //std::string useragent = "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/71.0.3578.49 Safari/537.36 zdx/1.0.0.16";
    //curl_easy_setopt(curl, CURLOPT_USERAGENT, useragent.c_str());
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    //curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);  // 30�볬ʱ

    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &downloadFileLenth);
    }
    curl_easy_cleanup(curl);
    return downloadFileLenth;
}

size_t CurlDownload::ReadCallback(void *ptr, size_t size, size_t nmemb, void *userp)
{
	PDATA_BUFFER databuf = (PDATA_BUFFER)userp;
	if (databuf == nullptr) {
		return 0;
	}
	size_t tocopy = size * nmemb;

	if (tocopy < 1 || !databuf->data_size) {
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

bool CurlDownload::post(std::string &sheader, std::string &sbody, std::string &err)
{
    bool bRet = false;
    CURLcode code = CURLE_OK;
	char error[CURL_ERROR_SIZE];
	memset(error, 0, sizeof(error));
	string versionString("User-Agent: zdx_upgrade ");
	versionString += "1.0.0.0";

	string contentTypeString;
	if (!content_type.empty() && content_type.length() > 0) {
		contentTypeString += "Content-Type: ";
		contentTypeString += content_type;
	}
	else {
		contentTypeString = "Content-Type: multipart/form-data; boundary=";
        contentTypeString += boundary;
	}

	auto curl_deleter = [] (CURL *curl) {curl_easy_cleanup(curl);};
	using Curl = unique_ptr<CURL, decltype(curl_deleter)>;
	Curl curl{curl_easy_init(), curl_deleter};
	if (curl) {
		struct curl_slist *header = nullptr;
		string sHeader, sBody;

		header = curl_slist_append(header, versionString.c_str());
		if (!contentTypeString.empty()) {
			header = curl_slist_append(header, contentTypeString.c_str());
		}
		header = curl_slist_append(header, "Expect:");  // �����Щ��������Ҫ 100 continue����
		header = curl_slist_append(header, "Accept: */*");
		header = curl_slist_append(header, ContentLength.c_str());

        curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 0L);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0);  // ����֤����
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2);  // ֤���м��SSL�����㷨�Ƿ����
		curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_HEADER, 0L);  // 0 sBodyֻ����body�壬1 sBody ����html��header+body
		curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header);
		curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteBodyCallback);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &sBody);
		curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
		curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &sHeader);
		//curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30);  // 30��

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
		curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
		//curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);  // sBody����������
		
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

bool CurlDownload::PrepareDataHeader()
{
	time_t t = time(NULL);
	char buf[128];
	memset(buf, 0, sizeof(128));
	sprintf_s(buf, 127, "%llX", t);
	ticks = buf;

	memset(buf, 0, 128);
	sprintf_s(buf, 127, "---------------------------%s", ticks.c_str());
	boundary = buf;

	return true;
}

bool CurlDownload::PrepareData(const std::string& name, const std::string& value)
{
	std::string data("");
	data += "--";
	data += boundary;
	data += "\r\n";
	data += "Content-Disposition: form-data; name=\"" + name + "\"";
	data += "\r\n\r\n";
	data += value;
	data += "\r\n";

	char* pbuf = nullptr;
	size_t bufsize = data_buffer.data_size + data.length();

	pbuf = new char[bufsize + 1];
	memset(pbuf, 0, sizeof(char)*(bufsize + 1));
	if (data_buffer.readptr) {
		memcpy(pbuf, data_buffer.readptr, data_buffer.data_size);
		delete[] data_buffer.readptr;
		data_buffer.readptr = nullptr;
	}
	memcpy(pbuf + data_buffer.data_size, data.c_str(), data.length());

	data_buffer.readptr = pbuf;
	data_buffer.delptr = data_buffer.readptr;
	data_buffer.data_size = bufsize;

	return true;
}

bool CurlDownload::PrepareDataFromFile(const std::string& name, const std::string& filename)
{
	fstream file;
	size_t filesize = 0;
	char* pfilebuf = nullptr;

	bool exists = os_file_exists(filename);
	if (!exists) {
		return false;
	}

	file.open(filename.c_str(), ios_base::in | ios_base::binary);
	if (!file.is_open()) {
        std::cout << "\n���ļ�[" << filename.c_str() << "]��ʧ�ܣ�\n" << std::endl;
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

	size_t r1 = filename.rfind('/');
	size_t r2 = filename.rfind('\\');
	size_t r = r1 > r2 ? r1 : r2;
	string sname("tmp.tmp");
	if (r > 0) {
		sname = filename.substr(r + 1);
	}
	
	string data("");
	data += "--";
	data += boundary;
	data += "\r\n";
	data += "Content-Disposition: form-data; name=\"" + name + "\"; filename=\"" + sname + "\"";
	data += "\r\n";
	data += "Content-Type: image/jpeg";
	data += "\r\n\r\n";

	string lastline = "\r\n";

	size_t bufsize = data_buffer.data_size + data.length() + filesize + lastline.length();
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
	memcpy(ptr, data.c_str(), data.length());
	data_buffer.data_size += data.length();

	ptr = ptr + data.length();
	memcpy(ptr, pfilebuf, filesize);
	data_buffer.data_size += filesize;

	ptr = ptr + filesize;
	memcpy(ptr, lastline.c_str(), lastline.length());
	data_buffer.data_size += lastline.length();
	
	delete [] pfilebuf;
	pfilebuf = nullptr;

	return true;
}

bool CurlDownload::PrepareDataFoot(bool isFile)
{
	string foot = "";
	if (isFile) {
		foot += "\r\n";  // ������һ�����ļ���Ҫ���һ���س�����
	}
	foot += "--";
	foot += boundary;
	foot += "--\r\n";

	char* pbuf = nullptr;
	size_t bufsize = data_buffer.data_size + foot.length();
	pbuf = new char[bufsize + 1];
	memset(pbuf, 0, sizeof(char)*(bufsize + 1));
	if (data_buffer.readptr) {
		memcpy(pbuf, data_buffer.readptr, data_buffer.data_size);
		delete[] data_buffer.readptr;
		data_buffer.readptr = nullptr;
	}
	memcpy(pbuf + data_buffer.data_size, foot.c_str(), foot.length());

	data_buffer.readptr = pbuf;
	data_buffer.delptr = data_buffer.readptr;
	data_buffer.data_size = bufsize;

	//char buf[128];
	//memset(buf, 0, 128);
	//sprintf_s(buf, 127, "Content-Length: %d", data_buffer.data_size);
    std::ostringstream ss;
    ss << "Content-Length: " << data_buffer.data_size;
	ContentLength = ss.str();

	return true;
}

bool CurlDownload::os_file_exists(const std::string &filename)
{
    std::ifstream fin(filename, std::ios::in);
    if (fin.good()) {
        fin.close();
        return true;
    }
    return false;
}

bool CurlDownload::post_json(std::string &sheader, std::string &sbody, std::string &err)
{
    bool bRet = false;
    CURLcode code = CURLE_OK;
    char error[CURL_ERROR_SIZE];
    memset(error, 0, sizeof(error));
    string versionString("User-Agent: zdx_upgrade ");
    versionString += "1.0.0.0";

    string contentTypeString;
    contentTypeString = "Content-Type: application/x-www-form-urlencoded; application/json; charset=UTF-8";

    auto curl_deleter = [](CURL *curl) {curl_easy_cleanup(curl); };
    using Curl = unique_ptr<CURL, decltype(curl_deleter)>;
    Curl curl{ curl_easy_init(), curl_deleter };
    if (curl) {
        struct curl_slist *header = nullptr;
        string sHeader, sBody;

        header = curl_slist_append(header, versionString.c_str());
        if (!contentTypeString.empty()) {
            header = curl_slist_append(header, contentTypeString.c_str());
        }
        header = curl_slist_append(header, "Expect:");  // �����Щ��������Ҫ 100 continue����
        header = curl_slist_append(header, "Accept: */*");
        header = curl_slist_append(header, ContentLength.c_str());

        curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0);  // ����֤����
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 1);  // ֤���м��SSL�����㷨�Ƿ����
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HEADER, 0L);  // 0 sBodyֻ����body�壬1 sBody ����html��header+body
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header);
        curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error);
        //curl_easy_setopt(curl.get(), CURLOPT_TRANSFERTEXT, TRUE);
        //curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteBodyCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &sBody);
        curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
        curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &sHeader);
        //curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30);  // 30��

#ifdef CURL_DOES_CONVERSIONS
        curl_easy_setopt(curl.get(), CURLOPT_TRANSFERTEXT, 1L);
#endif
#if LIBCURL_VERSION_NUM >= 0x072400
        curl_easy_setopt(curl.get(), CURLOPT_SSL_ENABLE_ALPN, 0L);
#endif
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, (long)data_buffer.data_size);
        curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, ReadCallback);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, data_buffer);
        curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        //curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);  // sBody����������

        code = curl_easy_perform(curl.get());
        if (code != CURLE_OK) {
            sheader = "";
            sbody = "";
            err = error;
            bRet = false;
        }
        else {
            sheader = sHeader;
            sbody = sBody;
            err = error;
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

bool CurlDownload::PrepareJsonData(const std::string& json)
{

    char* pbuf = nullptr;

    size_t bufsize = json.length();

    pbuf = new char[bufsize + 1];
    memset(pbuf, 0, sizeof(char)*(bufsize + 1));
    if (data_buffer.readptr) {
        memcpy(pbuf, data_buffer.readptr, data_buffer.data_size);
        delete[] data_buffer.readptr;
        data_buffer.readptr = nullptr;
    }
    memcpy(pbuf, json.c_str(), json.length());

    data_buffer.readptr = pbuf;
    data_buffer.delptr = data_buffer.readptr;
    data_buffer.data_size = bufsize;

    return true;
}

wstring CurlDownload::AsciiToUnicode(const string& str)
{
    // Ԥ��-�������п��ֽڵĳ���  
    int unicodeLen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
    // ��ָ�򻺳�����ָ����������ڴ�  
    wchar_t *pUnicode = (wchar_t*)malloc(sizeof(wchar_t)*unicodeLen);
    // ��ʼ�򻺳���ת���ֽ�  
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, pUnicode, unicodeLen);
    wstring ret_str = pUnicode;
    free(pUnicode);
    return ret_str;
}

string CurlDownload::UnicodeToUtf8(const wstring& wstr)
{
    // Ԥ��-�������ж��ֽڵĳ���  
    int ansiiLen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    // ��ָ�򻺳�����ָ����������ڴ�  
    char *pAssii = (char*)malloc(sizeof(char)*ansiiLen);
    // ��ʼ�򻺳���ת���ֽ�  
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, pAssii, ansiiLen, nullptr, nullptr);
    string ret_str = pAssii;
    free(pAssii);
    return ret_str;
}

bool CurlDownload::Download()
{
    bool bRet = false;
    int curl_code = CURLE_OK;
    int response_code = 0;
    do {
        bRet = DownloadOne(curl_code, response_code);
    } while (m_download_status != 2);
    
    return true;
}

bool CurlDownload::DownloadOne(int& curl_code, int& response_code)
{
    long long filesize = GetFileSize(m_local_filename);
    if (filesize == m_max_file_size) {
        m_download_status = 2;
        return true;
    }

    if (m_file == NULL) {
        m_file = fopen(m_local_filename.c_str(), "ab+");
        if (m_file == NULL)
        {
            return false;
        }
    }
    bool bRet = false;
    CURLcode code = CURLE_OK;
    char error[CURL_ERROR_SIZE];
    memset(error, 0, sizeof(error));
    string versionString("User-Agent: zdx_upgrade ");
    versionString += "1.0.0.0";

    string contentTypeString;
    contentTypeString = "Content-Type: application/x-www-form-urlencoded; application/json; charset=UTF-8";

    auto curl_deleter = [](CURL *curl) {curl_easy_cleanup(curl); };
    using Curl = unique_ptr<CURL, decltype(curl_deleter)>;
    Curl curl{ curl_easy_init(), curl_deleter };
    if (curl) {
        struct curl_slist *header = nullptr;
        string sHeader, sBody;

        header = curl_slist_append(header, versionString.c_str());
        if (!contentTypeString.empty()) {
            header = curl_slist_append(header, contentTypeString.c_str());
        }
        header = curl_slist_append(header, "Expect:");  // �����Щ��������Ҫ 100 continue����
        header = curl_slist_append(header, "Accept: */*");
        header = curl_slist_append(header, ContentLength.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header);

        curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 0L);
        // Զ��URL��֧�� http, https, ftp
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());

        //// ����User-Agent
        //std::string useragent = "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/71.0.3578.49 Safari/537.36 zdx/1.0.0.16";
        //curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, useragent.c_str());

        // �����ض����������
        curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 10);

        // ����301��302��ת����location
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POST, 0L);

        // �������ݻص�����
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteDownloadData);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, this);

        // ���Ȼص�����
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(curl.get(), CURLOPT_PROGRESSDATA, this);
        curl_easy_setopt(curl.get(), CURLOPT_PROGRESSFUNCTION, DownloadProgress);

        m_download_file_size = GetLocalFileLenth(this);
        if (m_download_file_size > 0) {
            curl_easy_setopt(curl.get(), CURLOPT_RESUME_FROM_LARGE, m_download_file_size);
        }

        // ����������SSL��֤����ʹ��CA֤��
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 0L);

        //curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30);  // 30��


        // ��֤�������˷��͵�֤�飬Ĭ���� 2(��)��1���У���0�����ã�
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
        curl_code = curl_easy_perform(curl.get());

        // �ر��ļ�
        if (m_file)
        {
            fclose(m_file);
            m_file = NULL;
        }

        // ����ʧ��
        if (curl_code != CURLE_OK)
        {
            bRet = false;
        }

        // ��ȡ״̬��
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);

        curl_slist_free_all(header);

        if (data_buffer.delptr) {
            delete[] data_buffer.delptr;
            data_buffer.delptr = nullptr;
        }
    }
    return bRet;
}

bool CurlDownload::PrepareDownloadData(const std::string& filename)
{
    m_local_filename = filename;
    return true;
}

long long CurlDownload::GetMaxDownloadSize()
{
    return m_max_file_size;
}
int CurlDownload::GetCurrentProgress()
{
    return m_current_progress;
}