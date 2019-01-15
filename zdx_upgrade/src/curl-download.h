#pragma once
#include <string>
#include <atomic>
#include "windows.h"

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

// ����ṹ�壬��δ������zdx.exe�ṩ���ݵģ�����zdx_upgrade.exe�Ĳ�����ͨ�������д��ݣ�ע�⹲���ڴ��С
struct zdx_upgrade_data {
    int        zdx_upgrade_status;                 // ����״̬
    long long  zdx_upgrade_max_file_size;          // ������ļ�(zdx_installer.exe)�ܴ�С
    long long  zdx_upgrade_download_size;          // �Ѿ����ش�С
    char       zdx_upgrade_md5[32 + 1];              // ������ļ�md5��
    char       zdx_upgrade_version[32];            // ����˷��ذ汾��
    char       zdx_upgrade_memo[256];              // �����������������255���ַ����ض�
    char       zdx_upgrade_filename[MAX_PATH];     // ����˷����ļ�����(ֻ���ļ���:zdx_install.exe,�� zdx_install_1.0.0.16.exe��������url)
    char       zdx_upgrade_api_url[MAX_PATH];      // ��ȡ�Զ����°�api��ַ
    char       zdx_upgrade_type[64];               // ���°������ͣ�zdx_browser_win32_upgrade��zdx_browser_win64_upgrade
    char       zdx_upgrade_client_md5[32 + 1];       // �ϴθ��°���md5�룬�����������ļ���
    char       zdx_upgrade_current_version[32];    // �ͻ��˰汾
    char       zdx_upgrade_client_path[MAX_PATH];  // �ͻ��˰�װ·��
    char       zdx_upgrade_url[512];               // ��װ������url��ַ
    int        zdx_upgrade_mode;                   // ���µ���ģʽ, 1:ֻ��ȡ�������Ϣ,10:���ظ���, 20:���ذ�װ, 30:ִ��ȫ������
    zdx_upgrade_data() {
        memset(this, 0, sizeof(zdx_upgrade_data));
    }
};

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
    struct zdx_upgrade_data m_zud;
    HANDLE m_hmap;

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
        struct zdx_upgrade_data& zud,
        HANDLE& hMap,
		std::string content_type_ = std::string()
	)
		: url(url_), m_zud(zud), m_hmap(hMap), content_type(content_type_), m_file(NULL), m_local_filename(""),
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
