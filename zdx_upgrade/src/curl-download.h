#pragma once
#include <string>
#include <atomic>

#ifndef DATA_BUFFER_
#define DATA_BUFFER_
typedef struct data_buffer_ {
	char *readptr;     // ������д����͵�����
	char *delptr;      // ������ݵ�ԭʼ�ڴ�ָ���ַ���ͷ��ڴ�ʹ��
	size_t data_size;  // ���������ݵĳ���
	data_buffer_() {
		memset(this, 0, sizeof(DATA_BUFFER));
	}
}DATA_BUFFER, *PDATA_BUFFER;
#endif

class CurlDownload{

private:
	std::string ticks;                    // ʱ���
	std::string boundary;                 // 
	std::string ContentLength;            // 
    FILE * m_file;                        // �����ļ��ı����ļ����
    std::string m_local_filename;         // ���ر���������ļ�·��
    std::atomic<bool> m_download_cancel;  // ȡ������
    int       m_download_status;          // ���ر�־�� 0:δ����, 1:������, 2:�������
    long long m_max_file_size;            // �����ļ��ܴ�С
    long long m_download_file_size;       // �Ѿ����ص���������С���û��ϵ�������¼�����ܽ���
    std::atomic<int> m_current_progress;  // ��ǰ���ؽ���(0, 100)

private:
	std::string url;
	std::string content_type;
	DATA_BUFFER data_buffer;
	
	// ��ȡurl����� response ͷ����Ϣ
	static size_t WriteBodyCallback(char *ptr, size_t size, size_t nmemb, std::string &str);
	// ��ȡurl����� response [ͷ�� + ����] ��Ϣ
	static size_t WriteHeaderCallback(char *ptr, size_t size, size_t nmemb, std::string &str);
    // ��ȡrul�����response��Ϣ��json��ʽ������
    static size_t WriteJsonData(void *ptr, size_t size, size_t nmemb, void *stream);
	// ��url����д��request�� post ����
	static size_t ReadCallback(void *ptr, size_t size, size_t nmemb, void *userp);
    // ���ػص�����
    static size_t WriteDownloadData(void* buffer, size_t size, size_t nmemb, void *stream);

    static size_t DownloadProgress(void* buffer, double dltotal, double dlnow, double ultotal, double ulnow);

    static long long GetLocalFileLenth(void* stream);

    double GetDownloadFileLenth(const char *url);
    long long GetFileSize(const std::string &filename);

    bool os_file_exists(const std::string &filename);
    std::wstring AsciiToUnicode(const std::string& str);
    std::string UnicodeToUtf8(const std::wstring& wstr);
    bool DownloadOne(int& curl_code, int& response_code);

public:
	inline CurlDownload(
		std::string url_,
		std::string content_type_ = std::string()
	)
		: url(url_), content_type(content_type_), m_file(NULL), m_local_filename(""), 
        m_download_cancel(false), m_download_file_size(0), m_download_status(0)
	{
        m_max_file_size = (long long)GetDownloadFileLenth(url.c_str());
    };

	// �����ڴ��������
	bool PrepareDataHeader();
	bool PrepareData(const std::string& name, const std::string& value);
	bool PrepareDataFromFile(const std::string& name, const std::string& filename);
	bool PrepareDataFoot(bool isFile = false);
    bool post(std::string &sheader, std::string &sbody, std::string &err);
    bool post_json(std::string &sheader, std::string &sbody, std::string &err);
    bool PrepareJsonData(const std::string& json);
    bool Download();
    bool PrepareDownloadData(const std::string& filename);
    FILE *GetFileHandle();
    std::string GetLocalFileName();
    bool GetDownLoadCancel();
    void SetDownLoadStop();
    long long GetMaxDownloadSize();
    int GetCurrentProgress();
};
