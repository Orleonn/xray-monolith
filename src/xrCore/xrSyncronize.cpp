#include "stdafx.h"
#include "profiler.h"

#ifdef PROFILE_CRITICAL_SECTIONS
static add_profile_portion_callback add_profile_portion = 0;
void set_add_profile_portion(add_profile_portion_callback callback)
{
    add_profile_portion = callback;
}

struct profiler
{
    u64 m_time;
    LPCSTR m_timer_id;

    IC profiler::profiler(LPCSTR timer_id)
    {
        if (!add_profile_portion)
            return;

        m_timer_id = timer_id;
        m_time = CPU::QPC();
    }

    IC profiler::~profiler()
    {
        if (!add_profile_portion)
            return;

        u64 time = CPU::QPC();
        (*add_profile_portion)(m_timer_id, time - m_time);
    }
};
#endif // PROFILE_CRITICAL_SECTIONS

using XRAY_CRITICAL_SECTION_HANDLE = CRITICAL_SECTION;

#ifdef PROFILE_CRITICAL_SECTIONS
xrCriticalSection::xrCriticalSection(LPCSTR id) : m_id(id)
#else // PROFILE_CRITICAL_SECTIONS
xrCriticalSection::xrCriticalSection()
#endif // PROFILE_CRITICAL_SECTIONS
{
	pmutex = xr_alloc<XRAY_CRITICAL_SECTION_HANDLE>(1);
	InitializeCriticalSection((XRAY_CRITICAL_SECTION_HANDLE*)pmutex);
}

xrCriticalSection::~xrCriticalSection()
{
	DeleteCriticalSection((XRAY_CRITICAL_SECTION_HANDLE*)pmutex);
	xr_free(pmutex);
}

#ifdef DEBUG
extern void OutputDebugStackTrace(const char* header);
#endif // DEBUG

void xrCriticalSection::Enter()
{
#ifdef PROFILE_CRITICAL_SECTIONS
# if 0//def DEBUG
    static bool show_call_stack = false;
    if (show_call_stack)
        OutputDebugStackTrace("----------------------------------------------------");
# endif // DEBUG
    profiler temp(m_id);
#endif // PROFILE_CRITICAL_SECTIONS
	EnterCriticalSection((XRAY_CRITICAL_SECTION_HANDLE*)pmutex);
}

void xrCriticalSection::Leave()
{
	LeaveCriticalSection((XRAY_CRITICAL_SECTION_HANDLE*)pmutex);
}

BOOL xrCriticalSection::TryEnter()
{
	return TryEnterCriticalSection((XRAY_CRITICAL_SECTION_HANDLE*)pmutex);
}

xrCriticalSection::raii::raii(xrCriticalSection* critical_section)
	: critical_section(critical_section)
{
	VERIFY(critical_section);
	critical_section->Enter();
}

xrCriticalSection::raii::~raii()
{
	critical_section->Leave();
}

xrSRWLock::xrSRWLock()
{
    InitializeSRWLock(&smutex);
}

void xrSRWLock::AcquireExclusive()
{
	PROF_EVENT("xrSRWLock::AcquireExclusive");
    AcquireSRWLockExclusive(&smutex);
}

void xrSRWLock::ReleaseExclusive()
{
	PROF_EVENT("xrSRWLock::ReleaseExclusive");
    ReleaseSRWLockExclusive(&smutex);
}

void xrSRWLock::AcquireShared()
{
	PROF_EVENT("xrSRWLock::AcquireShared");
    AcquireSRWLockShared(&smutex);
}

void xrSRWLock::ReleaseShared()
{
	PROF_EVENT("xrSRWLock::ReleaseShared");
    ReleaseSRWLockShared(&smutex);
}

BOOL xrSRWLock::TryAcquireExclusive()
{
    return TryAcquireSRWLockExclusive(&smutex);
}

BOOL xrSRWLock::TryAcquireShared()
{
    return TryAcquireSRWLockShared(&smutex);
}


xrSRWLockGuard::xrSRWLockGuard(xrSRWLock* lock, bool shared)
    : lock(lock), shared(shared)
{
    if (shared)
        lock->AcquireShared();
    else
        lock->AcquireExclusive();
}

xrSRWLockGuard::xrSRWLockGuard(xrSRWLock& lock, bool shared)
    : lock(&lock), shared(shared)
{
    if (shared)
        lock.AcquireShared();
    else
        lock.AcquireExclusive();
}

xrSRWLockGuard::~xrSRWLockGuard()
{
    if (shared)
        lock->ReleaseShared();
    else
        lock->ReleaseExclusive();
}



xrConditionVariable::xrConditionVariable()
    : handle{}
{
    InitializeConditionVariable(&handle);
}

bool xrConditionVariable::WaitFor(xrSRWLock& mtx, u32 milliseconds, bool shared)
{
    const ULONG flag = shared ? CONDITION_VARIABLE_LOCKMODE_SHARED : 0;
    return static_cast<bool>(SleepConditionVariableSRW(&handle, &mtx.smutex, milliseconds, flag));
}

bool xrConditionVariable::WaitFor(xrCriticalSection& cs, u32 milliseconds)
{
    XRAY_CRITICAL_SECTION_HANDLE* const hCS = static_cast<XRAY_CRITICAL_SECTION_HANDLE*>(cs.pmutex);
    return static_cast<bool>(SleepConditionVariableCS(&handle, hCS, milliseconds));
}

void xrConditionVariable::WakeOne()
{
    WakeConditionVariable(&handle);
}

void xrConditionVariable::WakeAll()
{
    WakeAllConditionVariable(&handle);
}
