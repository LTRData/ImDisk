#include <vcclr.h>
#include <stdlib.h>
#include <memory.h>

typedef size_t ssize_t;
typedef __int64 off_t_64;

#include "..\devio.h"

//#define DEBUG

#ifdef DEBUG
#define DbgMsg(m) System::Console::Out->WriteLine(m)
#else
#define DbgMsg(m)
#endif

#define ErrMsg(m) System::Console::Error->WriteLine(m)

using namespace System;
using namespace System::Reflection;
using namespace System::IO;

public ref class StreamCreator abstract sealed
{
	static Stream ^OpenFileStream(String ^filename, Boolean read_only)
	{
		FileAccess faccess;
		if (read_only)
			faccess = FileAccess::Read;
		else
			faccess = FileAccess::ReadWrite;

		return gcnew FileStream(filename, FileMode::Open, faccess);
	}
};

ssize_t
	__cdecl
	dllread(int fd, void *buf, size_t size,	off_t_64 offset)
{
	try
	{
		Stream ^strm = *(gcroot<Stream^>*)fd;

		strm->Position = offset;

		array<Byte> ^managed_buf = gcnew array<Byte>(size);
		
		Int32 bytes_read = strm->Read(managed_buf, 0, size);
		if (bytes_read <= 0)
			return bytes_read;

		pin_ptr<unsigned char> managed_buf_ptr = &managed_buf[0];
		memcpy(buf, managed_buf_ptr, bytes_read);
		managed_buf_ptr = nullptr;

		return bytes_read;
	}
	catch (Exception ^ex)
	{
		ErrMsg(ex->ToString());
		return -1;
	}
}

ssize_t
	__cdecl
	dllwrite(int fd, void *buf, size_t size, off_t_64 offset)
{
	try
	{
		Stream ^strm = *(gcroot<Stream^>*)fd;

		strm->Position = offset;

		array<Byte> ^managed_buf = gcnew array<Byte>(size);
		
		pin_ptr<unsigned char> managed_buf_ptr = &managed_buf[0];
		memcpy(managed_buf_ptr, buf, size);
		managed_buf_ptr = nullptr;

		strm->Write(managed_buf, 0, size);

		return size;
	}
	catch (Exception ^ex)
	{
		ErrMsg(ex->ToString());
		return -1;
	}
}

int
	__cdecl
	dllclose(int fd)
{
	try
	{
		gcroot<Stream^> *gcroot_strm_ptr = (gcroot<Stream^> *)fd;

		Stream ^strm = *gcroot_strm_ptr;

		strm->Close();

		delete gcroot_strm_ptr;

		return 0;
	}
	catch (Exception ^ex)
	{
		ErrMsg(ex->ToString());
		return -1;
	}
}

int
	__cdecl
	dllopen(
		const char *str,
		int read_only,
		dllread_proc *dllread_ptr, 
		dllwrite_proc *dllwrite_ptr, 
		dllclose_proc *dllclose_ptr,
		off_t_64 *size)
{
	try
	{
		*dllread_ptr = dllread;
		*dllwrite_ptr = dllwrite;
		*dllclose_ptr = dllclose;

		array<String^> ^delimiters = gcnew array<String^>(1);
		delimiters[0] = "::";
		array<String^> ^arguments = (gcnew String(str))->Split(delimiters, StringSplitOptions::None);
		String ^assemblyfile = arguments[0];
		String ^classname = arguments[1];
		String ^methodname = arguments[2];
		array<Object^> ^methodarguments = gcnew array<Object^>(2);
		methodarguments[0] = arguments[3];
		methodarguments[1] = read_only ? true : false;

		gcroot<Object^> *gcobjptr = NULL;

		Assembly ^assembly = Assembly::Load(AssemblyName::GetAssemblyName(assemblyfile));

		array<Type^> ^parametertypes = gcnew array<Type^>(2);
		parametertypes[0] = String::typeid;
		parametertypes[1] = Boolean::typeid;

		Type ^type = assembly->GetType(classname);
		if (type == nullptr)
			throw gcnew Exception("Class not found: " + classname);

		MethodInfo ^method = type->GetMethod(
			methodname,
			BindingFlags::Static | BindingFlags::InvokeMethod | BindingFlags::Public | BindingFlags::NonPublic,
			nullptr,
			parametertypes,
			nullptr);

		if (method == nullptr)
		{
			parametertypes[1] = FileAccess::typeid;
			methodarguments[1] = read_only ? FileAccess::Read : FileAccess::ReadWrite;

			method = type->GetMethod(
				methodname,
				BindingFlags::Static | BindingFlags::InvokeMethod | BindingFlags::Public | BindingFlags::NonPublic,
				nullptr,
				parametertypes,
				nullptr);
		}

		if (method == nullptr)
			throw gcnew Exception("Method not found: " + classname + "::" + methodname);

		if (!Stream::typeid->IsAssignableFrom(method->ReturnType))
			throw gcnew Exception("Method has wrong return Type: " + classname + "::" + methodname);

		Stream ^strm = (Stream^)method->Invoke(nullptr, methodarguments);
		if (strm == nullptr)
			throw gcnew Exception("Null Stream reference returned");

		*size = strm->Length;

		return (int)new gcroot<Stream^>(strm);
	}
	catch (Exception ^ex)
	{
		ErrMsg(ex->ToString());
		return -1;
	}
}
