#pragma once
#include <string>
#include <atomic>
#include "windows.h"

#ifndef DATA_BUFFER_
#define DATA_BUFFER_
typedef struct data_buffer_ {
	char *readptr;     // 存放所有待发送的数据
	char *delptr;      // 存放数据的原始内存指针地址，释放内存使用
	size_t data_size;  // 待发送数据的长度
	data_buffer_() {
		memset(this, 0, sizeof(DATA_BUFFER));
	}
}DATA_BUFFER, *PDATA_BUFFER;
#endif

// 这个结构体，是未主进程zdx.exe提供数据的，调用zdx_upgrade.exe的参数，通过命令行传递，注意共享内存大小
struct zdx_upgrade_data {
    int        zdx_upgrade_status;                 // 更新状态
    long long  zdx_upgrade_max_file_size;          // 服务端文件(zdx_installer.exe)总大小
    long long  zdx_upgrade_download_size;          // 已经下载大小
    char       zdx_upgrade_md5[32 + 1];              // 服务端文件md5码
    char       zdx_upgrade_version[32];            // 服务端返回版本号
    char       zdx_upgrade_memo[256];              // 更新描述，如果超过255个字符，截断
    char       zdx_upgrade_filename[MAX_PATH];     // 服务端返回文件名称(只是文件名:zdx_install.exe,或 zdx_install_1.0.0.16.exe，不包含url)
    char       zdx_upgrade_api_url[MAX_PATH];      // 获取自动更新包api地址
    char       zdx_upgrade_type[64];               // 更新包的类型，zdx_browser_win32_upgrade，zdx_browser_win64_upgrade
    char       zdx_upgrade_client_md5[32 + 1];       // 上次更新包的md5码，保存在配置文件中
    char       zdx_upgrade_current_version[32];    // 客户端版本
    char       zdx_upgrade_client_path[MAX_PATH];  // 客户端安装路径
    char       zdx_upgrade_url[512];               // 安装包下载url地址
    int        zdx_upgrade_mode;                   // 更新调用模式, 1:只获取服务端信息,10:下载更新, 20:本地安装, 30:执行全部过程
    zdx_upgrade_data() {
        memset(this, 0, sizeof(zdx_upgrade_data));
    }
};

class CurlDownload{

private:
	std::string ticks;                    // 时间戳
	std::string boundary;                 // 
	std::string ContentLength;            // 
    FILE * m_file;                        // 下载文件的本地文件句柄
    std::string m_local_filename;         // 本地保存的完整文件路径
    std::atomic<bool> m_download_cancel;  // 取消下载
    int       m_download_status;          // 下载标志， 0:未下载, 1:下载中, 2:下载完成
    long long m_max_file_size;            // 下载文件总大小
    long long m_download_file_size;       // 已经下载的数据量大小，用户断点续传记录下载总进度
    std::atomic<int> m_current_progress;  // 当前下载进度(0, 100)
    struct zdx_upgrade_data m_zud;
    HANDLE m_hmap;

private:
	std::string url;
	std::string content_type;
	DATA_BUFFER data_buffer;
	
	// 获取url请求的 response 头部信息
	static size_t WriteBodyCallback(char *ptr, size_t size, size_t nmemb, std::string &str);
	// 获取url请求的 response [头部 + 主体] 信息
	static size_t WriteHeaderCallback(char *ptr, size_t size, size_t nmemb, std::string &str);
    // 获取rul请求的response信息，json格式的请求
    static size_t WriteJsonData(void *ptr, size_t size, size_t nmemb, void *stream);
	// 向url请求写如request的 post 数据
	static size_t ReadCallback(void *ptr, size_t size, size_t nmemb, void *userp);
    // 下载回调函数
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

	// 向发送内存添加数据
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
