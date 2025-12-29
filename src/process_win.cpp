#include "process.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>  // _beginthreadex for MSVC's std::thread implementation

#include <string>
#include <thread>
#include <vector>

#include <stdexcept>

namespace ghx {

static std::string win32_error_message(DWORD err) {
  if (err == 0) return "no error";
  LPSTR buf = nullptr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD len = ::FormatMessageA(flags, nullptr, err, 0, reinterpret_cast<LPSTR>(&buf), 0, nullptr);
  std::string out = (len && buf) ? std::string(buf, buf + len) : std::string("unknown error");
  if (buf) ::LocalFree(buf);
  // Trim trailing newlines.
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

static std::wstring utf8_to_wide(const std::string& s) {
  if (s.empty()) return std::wstring();

  auto convert = [&](UINT codepage, DWORD flags) -> std::wstring {
    int needed = ::MultiByteToWideChar(codepage, flags, s.data(), (int)s.size(), nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring w;
    w.resize((size_t)needed);
    int written = ::MultiByteToWideChar(codepage, flags, s.data(), (int)s.size(), w.data(), needed);
    if (written != needed) return std::wstring();
    return w;
  };

  // Prefer UTF-8; fall back to the active ANSI code page for better Windows compatibility.
  if (auto w = convert(CP_UTF8, MB_ERR_INVALID_CHARS); !w.empty()) return w;
  if (auto w = convert(CP_ACP, 0); !w.empty()) return w;

  DWORD e = ::GetLastError();
  throw std::runtime_error("MultiByteToWideChar failed: " + win32_error_message(e));
}

// Windows command line quoting compatible with CreateProcess parsing rules.
// Equivalent to Python subprocess.list2cmdline behavior.
static std::wstring quote_arg_windows(const std::wstring& arg) {
  if (arg.empty()) return L"\"\"";

  const bool need_quotes =
      arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
  if (!need_quotes) return arg;

  std::wstring out;
  out.push_back(L'"');

  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      backslashes++;
      continue;
    }
    if (c == L'"') {
      // Escape all backslashes and the quote.
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
      continue;
    }
    // Normal char: emit accumulated backslashes as-is.
    if (backslashes) out.append(backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }

  // Escape backslashes that would otherwise escape the closing quote.
  if (backslashes) out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

static std::wstring build_command_line_windows(const std::vector<std::string>& args) {
  std::wstring cmd;
  for (size_t i = 0; i < args.size(); i++) {
    if (i) cmd.push_back(L' ');
    cmd += quote_arg_windows(utf8_to_wide(args[i]));
  }
  return cmd;
}

static void close_handle_if_valid(HANDLE& h) {
  if (h && h != INVALID_HANDLE_VALUE) {
    ::CloseHandle(h);
    h = INVALID_HANDLE_VALUE;
  }
}

static void read_all_from_handle(HANDLE h, std::string& out) {
  char buf[8192];
  while (true) {
    DWORD n = 0;
    BOOL ok = ::ReadFile(h, buf, (DWORD)sizeof(buf), &n, nullptr);
    if (ok) {
      if (n == 0) break;
      out.append(buf, buf + n);
      continue;
    }
    DWORD e = ::GetLastError();
    if (e == ERROR_BROKEN_PIPE) break;  // child closed pipe
    // Other errors: stop reading but keep what we got.
    break;
  }
  ::CloseHandle(h);
}

static void write_all_to_handle(HANDLE h, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    DWORD n = 0;
    BOOL ok = ::WriteFile(h, data.data() + off, (DWORD)(data.size() - off), &n, nullptr);
    if (!ok) {
      DWORD e = ::GetLastError();
      throw std::runtime_error("WriteFile(stdin) failed: " + win32_error_message(e));
    }
    off += (size_t)n;
  }
}

ProcessResult run_process(
    const std::vector<std::string>& args,
    const std::string& stdin_data,
    const std::string& working_dir) {
  if (args.empty()) {
    throw std::invalid_argument("run_process: args is empty");
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE child_stdin_read = INVALID_HANDLE_VALUE;
  HANDLE parent_stdin_write = INVALID_HANDLE_VALUE;
  HANDLE parent_stdout_read = INVALID_HANDLE_VALUE;
  HANDLE child_stdout_write = INVALID_HANDLE_VALUE;
  HANDLE parent_stderr_read = INVALID_HANDLE_VALUE;
  HANDLE child_stderr_write = INVALID_HANDLE_VALUE;

  if (!::CreatePipe(&child_stdin_read, &parent_stdin_write, &sa, 0)) {
    DWORD e = ::GetLastError();
    throw std::runtime_error("CreatePipe(stdin) failed: " + win32_error_message(e));
  }
  if (!::SetHandleInformation(parent_stdin_write, HANDLE_FLAG_INHERIT, 0)) {
    DWORD e = ::GetLastError();
    close_handle_if_valid(child_stdin_read);
    close_handle_if_valid(parent_stdin_write);
    throw std::runtime_error("SetHandleInformation(stdin_write) failed: " + win32_error_message(e));
  }

  if (!::CreatePipe(&parent_stdout_read, &child_stdout_write, &sa, 0)) {
    DWORD e = ::GetLastError();
    close_handle_if_valid(child_stdin_read);
    close_handle_if_valid(parent_stdin_write);
    throw std::runtime_error("CreatePipe(stdout) failed: " + win32_error_message(e));
  }
  if (!::SetHandleInformation(parent_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
    DWORD e = ::GetLastError();
    close_handle_if_valid(child_stdin_read);
    close_handle_if_valid(parent_stdin_write);
    close_handle_if_valid(parent_stdout_read);
    close_handle_if_valid(child_stdout_write);
    throw std::runtime_error("SetHandleInformation(stdout_read) failed: " + win32_error_message(e));
  }

  if (!::CreatePipe(&parent_stderr_read, &child_stderr_write, &sa, 0)) {
    DWORD e = ::GetLastError();
    close_handle_if_valid(child_stdin_read);
    close_handle_if_valid(parent_stdin_write);
    close_handle_if_valid(parent_stdout_read);
    close_handle_if_valid(child_stdout_write);
    throw std::runtime_error("CreatePipe(stderr) failed: " + win32_error_message(e));
  }
  if (!::SetHandleInformation(parent_stderr_read, HANDLE_FLAG_INHERIT, 0)) {
    DWORD e = ::GetLastError();
    close_handle_if_valid(child_stdin_read);
    close_handle_if_valid(parent_stdin_write);
    close_handle_if_valid(parent_stdout_read);
    close_handle_if_valid(child_stdout_write);
    close_handle_if_valid(parent_stderr_read);
    close_handle_if_valid(child_stderr_write);
    throw std::runtime_error("SetHandleInformation(stderr_read) failed: " + win32_error_message(e));
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdInput = child_stdin_read;
  si.hStdOutput = child_stdout_write;
  si.hStdError = child_stderr_write;

  PROCESS_INFORMATION pi{};

  std::wstring cmd = build_command_line_windows(args);
  // CreateProcess requires a writable buffer.
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  std::wstring wdir = working_dir.empty() ? std::wstring() : utf8_to_wide(working_dir);
  const wchar_t* wdir_ptr = working_dir.empty() ? nullptr : wdir.c_str();

  DWORD creation_flags = CREATE_NO_WINDOW;
  BOOL ok = ::CreateProcessW(
      /*lpApplicationName=*/nullptr,
      /*lpCommandLine=*/cmd_buf.data(),
      /*lpProcessAttributes=*/nullptr,
      /*lpThreadAttributes=*/nullptr,
      /*bInheritHandles=*/TRUE,
      /*dwCreationFlags=*/creation_flags,
      /*lpEnvironment=*/nullptr,
      /*lpCurrentDirectory=*/wdir_ptr,
      /*lpStartupInfo=*/&si,
      /*lpProcessInformation=*/&pi);

  // Parent no longer needs child-side handles.
  close_handle_if_valid(child_stdin_read);
  close_handle_if_valid(child_stdout_write);
  close_handle_if_valid(child_stderr_write);

  if (!ok) {
    DWORD e = ::GetLastError();
    close_handle_if_valid(parent_stdin_write);
    close_handle_if_valid(parent_stdout_read);
    close_handle_if_valid(parent_stderr_read);
    throw std::runtime_error("CreateProcessW failed: " + win32_error_message(e));
  }

  ProcessResult r;

  // Start draining stdout/stderr immediately to avoid deadlocks.
  std::thread t_out([&]() { read_all_from_handle(parent_stdout_read, r.stdout_str); });
  std::thread t_err([&]() { read_all_from_handle(parent_stderr_read, r.stderr_str); });

  try {
    if (!stdin_data.empty()) {
      write_all_to_handle(parent_stdin_write, stdin_data);
    }
  } catch (...) {
    // Keep going; still close stdin to signal EOF and wait for process.
  }
  close_handle_if_valid(parent_stdin_write);

  ::WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 0;
  if (!::GetExitCodeProcess(pi.hProcess, &exit_code)) {
    r.exit_code = -1;
  } else {
    r.exit_code = (int)exit_code;
  }

  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);

  if (t_out.joinable()) t_out.join();
  if (t_err.joinable()) t_err.join();

  return r;
}

}  // namespace ghx

#endif  // _WIN32


