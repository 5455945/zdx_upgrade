#include <Windows.h>
#include <string>
#include <process.h>
#include <aclapi.h>


class SharedMemory
{
public:
    SharedMemory(BOOL bReadOnly = FALSE) : m_hLock(NULL),
        m_hFileMap(NULL),
        m_pMemory(NULL),
        m_bReadOnly(FALSE),
        m_dwMappedSize(0),
        m_strName(L"")
    {

    }

    BOOL Create(const std::wstring& strName, DWORD dwSize)
    {
        if (dwSize <= 0)
            return FALSE;

        //SECURITY_ATTRIBUTES sa;
        //SECURITY_DESCRIPTOR sd;
        //InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        //SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
        //sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        //sa.lpSecurityDescriptor = &sd,
        //sa.bInheritHandle = FALSE;

        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, FALSE };
        SECURITY_DESCRIPTOR sd;
        ACL dacl;
        sa.lpSecurityDescriptor = &sd;
        if (!InitializeAcl(&dacl, sizeof(dacl), ACL_REVISION)) {
            return FALSE;
        }
        if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
            return FALSE;
        }
        if (!SetSecurityDescriptorDacl(&sd, TRUE, &dacl, FALSE)) {
            return FALSE;
        }

        HANDLE handle = ::CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE | SEC_COMMIT, 0, dwSize, strName.empty() ? NULL : strName.c_str());
        if (!handle)
            return FALSE;

        // 已经存在了
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            Close();
            return FALSE;
        }

        m_hFileMap = handle;
        m_dwMappedSize = dwSize;
        return TRUE;
    }

    BOOL Open(const std::wstring& strName, BOOL bReadOnly)
    {
        m_hFileMap = ::OpenFileMappingW(bReadOnly ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS, FALSE, strName.empty() ? NULL : strName.c_str());
        if (!m_hFileMap)
            return FALSE;

        m_bReadOnly = bReadOnly;
        return TRUE;
    }

    BOOL MapAt(DWORD dwOffset, DWORD dwSize)
    {
        if (!m_hFileMap)
            return FALSE;

        if (dwSize > ULONG_MAX)
            return FALSE;

        ULARGE_INTEGER ui;
        ui.QuadPart = static_cast<ULONGLONG>(dwOffset);

        m_pMemory = ::MapViewOfFile(m_hFileMap,
            m_bReadOnly ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS, ui.HighPart, ui.LowPart, dwSize);

        return (m_pMemory != NULL);
    }

    void Unmap()
    {
        if (m_pMemory)
        {
            ::UnmapViewOfFile(m_pMemory);
            m_pMemory = NULL;
        }
    }

    LPVOID GetMemory() const { return m_pMemory; }

    HANDLE GetHandle() const
    {
        return m_hFileMap;
    }

    // 锁定共享内存
    BOOL Lock(DWORD dwTime)
    {
        // 如果还没有创建锁就先创建一个
        if (!m_hLock)
        {
            std::wstring strLockName = m_strName;
            strLockName.append(L"_Lock");
            // 初始化的时候不被任何线程占用
            m_hLock = ::CreateMutexW(NULL, FALSE, strLockName.c_str());
            if (!m_hLock)
                return FALSE;
        }

        // 哪个线程最先调用等待函数就最先占用这个互斥量
        DWORD dwRet = ::WaitForSingleObject(m_hLock, dwTime);
        return (dwRet == WAIT_OBJECT_0 || dwRet == WAIT_ABANDONED);
    }

    void Unlock()
    {
        if (m_hLock)
        {
            ::ReleaseMutex(m_hLock);
        }
    }

    ~SharedMemory()
    {
        Close();
        if (m_hLock != NULL)
        {
            CloseHandle(m_hLock);
        }
    }

    void Close()
    {
        Unmap();

        if (m_hFileMap)
        {
            ::CloseHandle(m_hFileMap);
            m_hFileMap = NULL;
        }
    }

private:
    HANDLE m_hLock;
    HANDLE m_hFileMap;
    LPVOID m_pMemory;
    std::wstring m_strName;
    BOOL m_bReadOnly;
    DWORD m_dwMappedSize;

    SharedMemory(const SharedMemory& other);
    SharedMemory& operator = (const SharedMemory& other);
};
