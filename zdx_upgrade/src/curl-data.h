#pragma once

#include <string>

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

class CurlData {

private:
	std::string url;
	std::string content_type;
	DATA_BUFFER data_buffer;
	
	// 获取url请求的 response 头部信息
	static size_t WriteBodyCallback(char *ptr, size_t size, size_t nmemb, std::string &str);
	// 获取url请求的 response [头部 + 主体] 信息
	static size_t WriteHeaderCallback(char *ptr, size_t size, size_t nmemb, std::string &str);
	// 向url请求写如request的 post 数据
	static size_t ReadCallback(void *ptr, size_t size, size_t nmemb, void *userp);

    bool os_file_exists(const std::string &filename);

public:
	inline CurlData(
		std::string url_,
		std::string content_type_ = std::string()
	)
		: url(url_), content_type(content_type_)
	{};

	// 处理urlencode，有些接口不通用
	static size_t MyUrlEncode(void* dst, size_t& dst_len, const void* src, size_t len);
	// 向发送内存添加数据
	bool PrepareData(const std::string& name, const std::string& data);
	bool PrepareData(const std::string& name, void* data, size_t data_size);
	bool PrepareDataFromFile(const std::string& name, const std::string& filename);
    bool post(std::string &sheader, std::string &sbody, std::string &err);
};
