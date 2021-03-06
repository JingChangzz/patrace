#include "GraphicBuffer.hpp"
#include <unistd.h>
#include <sstream>

#include <string>
#include <cstdlib>
#include <iostream>

#include "common/os.hpp"

using std::string;

const int GRAPHICBUFFER_SIZE = 10240;

template<typename Func>
void setFuncPtr (Func*& funcPtr, const DynamicLibrary& lib, const string& symname)
{
    funcPtr = reinterpret_cast<Func*>(lib.getFunctionPtr(symname.c_str()));
}

#if defined(__aarch64__)
#   define CPU_ARM_64
#elif defined(__arm__) || defined(__ARM__) || defined(__ARM_NEON__) || defined(ARM_BUILD)
#   define CPU_ARM
#elif defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
#   define CPU_X86_64
#elif defined(__i386__) || defined(_M_X86) || defined(_M_IX86) || defined(X86_BUILD)
#   define CPU_X86
#else
#   warning "target CPU does not support ABI"
#endif


template <typename RT, typename T1, typename T2, typename T3, typename T4>
RT* callConstructor4 (void (*fptr)(), void* memory, T1 param1, T2 param2, T3 param3, T4 param4)
{
#if defined(CPU_ARM)
    // C1 constructors return pointer
    typedef RT* (*ABIFptr)(void*, T1, T2, T3, T4);
    (void)((ABIFptr)fptr)(memory, param1, param2, param3, param4);
    return reinterpret_cast<RT*>(memory);
#elif defined(CPU_ARM_64)
    // C1 constructors return void
    typedef void (*ABIFptr)(void*, T1, T2, T3, T4);
    ((ABIFptr)fptr)(memory, param1, param2, param3, param4);
    return reinterpret_cast<RT*>(memory);
#elif defined(CPU_X86) || defined(CPU_X86_64)
    // ctor returns void
    typedef void (*ABIFptr)(void*, T1, T2, T3, T4);
    ((ABIFptr)fptr)(memory, param1, param2, param3, param4);
    return reinterpret_cast<RT*>(memory);
#else
    return nullptr;
#endif
}

template <typename RT, typename T1, typename T2, typename T3, typename T4, typename T5>
RT* callConstructor5 (void (*fptr)(), void* memory, T1 param1, T2 param2, T3 param3, T4 param4, T5 param5)
{
#if defined(CPU_ARM)
    // C1 constructors return pointer
    typedef RT* (*ABIFptr)(void*, T1, T2, T3, T4, T5);
    (void)((ABIFptr)fptr)(memory, param1, param2, param3, param4, param5);
    return reinterpret_cast<RT*>(memory);
#elif defined(CPU_ARM_64)
    // C1 constructors return void
    typedef void (*ABIFptr)(void*, T1, T2, T3, T4, T5);
    ((ABIFptr)fptr)(memory, param1, param2, param3, param4, param5);
    return reinterpret_cast<RT*>(memory);
#elif defined(CPU_X86) || defined(CPU_X86_64)
    // ctor returns void
    typedef void (*ABIFptr)(void*, T1, T2, T3, T4, T5);
    ((ABIFptr)fptr)(memory, param1, param2, param3, param4, param5);
    return reinterpret_cast<RT*>(memory);
#else
    return nullptr;
#endif
}

template <typename T>
void callDestructor (void (*fptr)(), T* obj)
{
#if defined(CPU_ARM)
    // D1 destructor returns ptr
    typedef void* (*ABIFptr)(T* obj);
    (void)((ABIFptr)fptr)(obj);
#elif defined(CPU_ARM_64)
    // D1 destructor returns void
    typedef void (*ABIFptr)(T* obj);
    ((ABIFptr)fptr)(obj);
#elif defined(CPU_X86) || defined(CPU_X86_64)
    // dtor returns void
    typedef void (*ABIFptr)(T* obj);
    ((ABIFptr)fptr)(obj);
#endif
}

template<typename T1, typename T2>
T1* pointerToOffset (T2* ptr, size_t bytes)
{
    return reinterpret_cast<T1*>((uint8_t *)ptr + bytes);
}

static android::android_native_base_t* getAndroidNativeBase (android::GraphicBuffer* gb)
{
    return pointerToOffset<android::android_native_base_t>(gb, 2 * sizeof(void *));
}

#ifdef PLATFORM_64BIT
GraphicBufferFunctions graphicBufferFunctions("/system/lib64/libui.so");
//GraphicBufferFunctions graphicBufferFunctions("libui.so");
#define LIBNATIVEWINDOW "/system/lib64/libnativewindow.so"
#else
GraphicBufferFunctions graphicBufferFunctions("/system/lib/libui.so");
//GraphicBufferFunctions graphicBufferFunctions("libui.so");
#define LIBNATIVEWINDOW "/system/lib/libnativewindow.so"
#endif

HardwareBufferFunctions *hardwareBufferFunctions;

bool useGraphicBuffer = false;
bool useHardwareBuffer= false;

GraphicBufferFunctions::GraphicBufferFunctions(const char *fileName):
    library(fileName)
{
    if (library.getlibHandle())
    {
        useGraphicBuffer = true;
        useConstructor4 = true;
        setFuncPtr(constructor, library, "_ZN7android13GraphicBufferC1Ejjij");
        if (!constructor) {
            useConstructor4 = false;
            setFuncPtr(constructor, library, "_ZN7android13GraphicBufferC1EjjijNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
        }

        setFuncPtr(destructor, library, "_ZN7android13GraphicBufferD1Ev");
        setFuncPtr(getNativeBuffer, library, "_ZNK7android13GraphicBuffer15getNativeBufferEv");
        useLock3 = true;
        setFuncPtr(lock3, library, "_ZN7android13GraphicBuffer4lockEjPPv");
        if (!lock3) {
            useLock3 = false;
            setFuncPtr(lock5, library, "_ZN7android13GraphicBuffer4lockEjPPvPiS3_");
        }
        setFuncPtr(unlock, library, "_ZN7android13GraphicBuffer6unlockEv");
        setFuncPtr(initCheck, library, "_ZNK7android13GraphicBuffer9initCheckEv");
        setFuncPtr(lockYCbCr, library, "_ZN7android13GraphicBuffer9lockYCbCrEjP13android_ycbcr");
    }
    else
    {
        DBG_LOG("try to open %s!\n", LIBNATIVEWINDOW);
        hardwareBufferFunctions = new HardwareBufferFunctions(LIBNATIVEWINDOW);
    }
}

GraphicBuffer::GraphicBuffer(uint32_t width, uint32_t height, PixelFormat format, uint32_t usage):
    functions(&graphicBufferFunctions)
{
    // allocate memory for GraphicBuffer object
    void *const memory = malloc(GRAPHICBUFFER_SIZE);
    if (memory == nullptr) {
        std::cerr << "Could not alloc for GraphicBuffer" << std::endl;
        return;
    }

    int pid = getpid();
    std::stringstream ss;
    ss << pid;
    std::string s = "[GraphicBuffer pid " + ss.str() + ']';
    try {
        android::GraphicBuffer* gb;
        if (graphicBufferFunctions.useConstructor4) {
            gb = callConstructor4<android::GraphicBuffer, uint32_t, uint32_t, PixelFormat, uint32_t>(
                functions->constructor,
                memory,
                width,
                height,
                format,
                usage);
        }
        else {
            gb = callConstructor5<android::GraphicBuffer, uint32_t, uint32_t, PixelFormat, uint32_t, std::string>(
                functions->constructor,
                memory,
                width,
                height,
                format,
                usage,
                s);
        }

        android::android_native_base_t* const base = getAndroidNativeBase(gb);
        status_t ctorStatus = functions->initCheck(gb);

        if (ctorStatus) {
            // ctor failed
            callDestructor<android::GraphicBuffer>(functions->destructor, gb);
            std::cerr << "GraphicBuffer ctor failed, initCheck returned "  << ctorStatus << std::endl;
        }

        // check object layout
        if (base->magic != 0x5f626672u) // "_bfr"
            std::cerr << "GraphicBuffer layout unexpected" << std::endl;

        // check object version
        const uint32_t expectedVersion = sizeof(void *) == 4 ? 96 : 168;
        if (base->version != expectedVersion)
            std::cerr << "GraphicBuffer version unexpected" << std::endl;

        base->incRef(base);
        impl = gb;
    } catch (...) {
        DBG_LOG("Some exceptions are thrown in the constructor of GraphicBuffer!\n");
        free(memory);
        throw;
    }
}

GraphicBuffer::GraphicBuffer(void *ptr):
    functions(&graphicBufferFunctions)
{
    impl = (android::GraphicBuffer*)ptr;
}

GraphicBuffer::~GraphicBuffer()
{
    if (impl) {
        android::android_native_base_t* const base = getAndroidNativeBase(impl);
        base->decRef(base);
        free(impl);
    }
}

status_t GraphicBuffer::lockYCbCr(uint32_t usage, android_ycbcr* ycbcr)
{
    return functions->lockYCbCr(impl, usage, ycbcr);
}

status_t GraphicBuffer::lock(uint32_t usage, void** vaddr)
{
    if (graphicBufferFunctions.useLock3)
        return functions->lock3(impl, usage, vaddr);
    int outBytesPerPixel, outBytesPerStride;
    return functions->lock5(impl, usage, vaddr, &outBytesPerPixel, &outBytesPerStride);
}

status_t GraphicBuffer::unlock()
{
    return functions->unlock(impl);
}

ANativeWindowBuffer *GraphicBuffer::getNativeBuffer() const
{
    return functions->getNativeBuffer(impl);
}

///////////////////////////////////////////////////////////////////

int GraphicBuffer::getWidth() const
{
    return ((android::android_native_buffer_t*)getNativeBuffer())->width;
}

int GraphicBuffer::getHeight() const
{
    return ((android::android_native_buffer_t*)getNativeBuffer())->height;
}

int GraphicBuffer::getStride() const
{
    return ((android::android_native_buffer_t*)getNativeBuffer())->stride;
}

int GraphicBuffer::getFormat() const
{
    return ((android::android_native_buffer_t*)getNativeBuffer())->format;
}

int GraphicBuffer::getUsage() const
{
    return ((android::android_native_buffer_t*)getNativeBuffer())->usage;
}

///////////////////////////////////////////////////////////////////

void GraphicBuffer::setWidth(int width)
{
    ((android::android_native_buffer_t*)getNativeBuffer())->width = width;
}

void GraphicBuffer::setHeight(int height)
{
    ((android::android_native_buffer_t*)getNativeBuffer())->height = height;
}

void GraphicBuffer::setStride(int stride)
{
    ((android::android_native_buffer_t*)getNativeBuffer())->stride = stride;
}

void GraphicBuffer::setFormat(int format)
{
    ((android::android_native_buffer_t*)getNativeBuffer())->format = format;
}

void GraphicBuffer::setUsage(int usage)
{
    ((android::android_native_buffer_t*)getNativeBuffer())->usage = usage;
}

///////////////////////////////////////////////////////////////////

void *GraphicBuffer::getImpl() const
{
    return impl;
}


HardwareBufferFunctions::HardwareBufferFunctions(const char *fileName):
    library(fileName)
{
    if (library.getlibHandle())
    {
        useHardwareBuffer = true;
        setFuncPtr(allocate, library, "AHardwareBuffer_allocate");
        setFuncPtr(lock, library, "AHardwareBuffer_lock");
        setFuncPtr(describe, library, "AHardwareBuffer_describe");
        setFuncPtr(unlock, library, "AHardwareBuffer_unlock");
        setFuncPtr(acquire, library, "AHardwareBuffer_acquire");
        setFuncPtr(release, library, "AHardwareBuffer_release");
    }
    else
    {
        DBG_LOG("Fail to open either libui.so or libnativewindow.so. Exit!\n");
        throw OpenLibFailedException();
    }
}

HardwareBuffer::HardwareBuffer(uint32_t width, uint32_t height, uint32_t format, uint32_t usage):functions(hardwareBufferFunctions)
{
    AHardwareBuffer_Desc ahb_desc = {0};

    ahb_desc.rfu0 = 0;
    ahb_desc.rfu1 = 0;
    ahb_desc.width = width;
    ahb_desc.height = height;
    ahb_desc.layers = 1;
    ahb_desc.format = format;
    ahb_desc.usage = usage;

    if (functions->allocate(&ahb_desc, &ahb_impl) != 0)
    {
        DBG_LOG("HardwareBuffer constructor fails to allocate AHardwareBuffer obj.\n");
    }
}

HardwareBuffer::HardwareBuffer(void *ptr):functions(hardwareBufferFunctions)
{
    ahb_impl = (AHardwareBuffer *)ptr;
}

HardwareBuffer::~HardwareBuffer()
{
    functions->release(ahb_impl);
}

int HardwareBuffer::allocate(const AHardwareBuffer_Desc* desc)
{
    int ret = functions->allocate(desc, &ahb_impl);

    if (ret != 0)
    {
        DBG_LOG("HardwareBuffer->allocate() fail.\n");
    }
    return ret;
}

void HardwareBuffer::describe(AHardwareBuffer_Desc* outDesc)
{
    functions->describe(ahb_impl, outDesc);
}

int HardwareBuffer::lock(uint64_t usage, int32_t fence, const ARect* rect, void **outVirtAddress)
{
    return functions->lock(ahb_impl, usage, fence, rect, outVirtAddress);
}

int HardwareBuffer::unlock(int32_t* fence)
{
    return functions->unlock(ahb_impl, fence);
}

void HardwareBuffer::acquire()
{
    functions->acquire(ahb_impl);
}

void HardwareBuffer::release()
{
    functions->release(ahb_impl);
}

#define EGL_EGLEXT_PROTOTYPES
#include "EGL/eglext.h"

EGLClientBuffer HardwareBuffer::eglGetNativeClientBufferANDROID(void *funcPtr)
{
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC __eglGetNativeClientBufferANDROID = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)funcPtr;

    if (NULL != __eglGetNativeClientBufferANDROID)
    {
        return __eglGetNativeClientBufferANDROID(ahb_impl);
    }
    return NULL;
}

void *HardwareBuffer::getImpl() const
{
    return ahb_impl;
}
