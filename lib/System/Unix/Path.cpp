//===- llvm/System/Unix/Path.cpp - Unix Path Implementation -----*- C++ -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Reid Spencer and is distributed under the 
// University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific portion of the Path class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include "llvm/Config/alloca.h"
#include "Unix.h"
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if HAVE_UTIME_H
#include <utime.h>
#endif
#if HAVE_TIME_H
#include <time.h>
#endif
#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif


namespace llvm {
using namespace sys;

Path::Path(const std::string& unverified_path) : path(unverified_path) {
  if (unverified_path.empty())
    return;
  if (this->isValid()) 
    return;
  // oops, not valid.
  path.clear();
  ThrowErrno(unverified_path + ": path is not valid");
}

bool 
Path::isValid() const {
  if (path.empty()) 
    return false;
  else if (path.length() >= MAXPATHLEN)
    return false;
#if defined(HAVE_REALPATH)
  char pathname[MAXPATHLEN];
  if (0 == realpath(path.c_str(), pathname))
    if (errno != EACCES && errno != EIO && errno != ENOENT && errno != ENOTDIR)
      return false;
#endif
  return true;
}

Path
Path::GetRootDirectory() {
  Path result;
  result.setDirectory("/");
  return result;
}

Path
Path::GetTemporaryDirectory() {
#if defined(HAVE_MKDTEMP)
  // The best way is with mkdtemp but that's not available on many systems, 
  // Linux and FreeBSD have it. Others probably won't.
  char pathname[MAXPATHLEN];
  strcpy(pathname,"/tmp/llvm_XXXXXX");
  if (0 == mkdtemp(pathname))
    ThrowErrno(std::string(pathname) + ": Can't create temporary directory");
  Path result;
  result.setDirectory(pathname);
  assert(result.isValid() && "mkdtemp didn't create a valid pathname!");
  return result;
#elif defined(HAVE_MKSTEMP)
  // If no mkdtemp is available, mkstemp can be used to create a temporary file
  // which is then removed and created as a directory. We prefer this over
  // mktemp because of mktemp's inherent security and threading risks. We still
  // have a slight race condition from the time the temporary file is created to
  // the time it is re-created as a directoy. 
  char pathname[MAXPATHLEN];
  strcpy(pathname, "/tmp/llvm_XXXXXX");
  int fd = 0;
  if (-1 == (fd = mkstemp(pathname)))
    ThrowErrno(std::string(pathname) + ": Can't create temporary directory");
  ::close(fd);
  ::unlink(pathname); // start race condition, ignore errors
  if (-1 == ::mkdir(pathname, S_IRWXU)) // end race condition
    ThrowErrno(std::string(pathname) + ": Can't create temporary directory");
  Path result;
  result.setDirectory(pathname);
  assert(result.isValid() && "mkstemp didn't create a valid pathname!");
  return result;
#elif defined(HAVE_MKTEMP)
  // If a system doesn't have mkdtemp(3) or mkstemp(3) but it does have
  // mktemp(3) then we'll assume that system (e.g. AIX) has a reasonable
  // implementation of mktemp(3) and doesn't follow BSD 4.3's lead of replacing
  // the XXXXXX with the pid of the process and a letter. That leads to only
  // twenty six temporary files that can be generated.
  char pathname[MAXPATHLEN];
  strcpy(pathname, "/tmp/llvm_XXXXXX");
  char *TmpName = ::mktemp(pathname);
  if (TmpName == 0)
    throw std::string(TmpName) + ": Can't create unique directory name";
  if (-1 == ::mkdir(TmpName, S_IRWXU))
    ThrowErrno(std::string(TmpName) + ": Can't create temporary directory");
  Path result;
  result.setDirectory(TmpName);
  assert(result.isValid() && "mktemp didn't create a valid pathname!");
  return result;
#else
  // This is the worst case implementation. tempnam(3) leaks memory unless its
  // on an SVID2 (or later) system. On BSD 4.3 it leaks. tmpnam(3) has thread
  // issues. The mktemp(3) function doesn't have enough variability in the
  // temporary name generated. So, we provide our own implementation that 
  // increments an integer from a random number seeded by the current time. This
  // should be sufficiently unique that we don't have many collisions between
  // processes. Generally LLVM processes don't run very long and don't use very
  // many temporary files so this shouldn't be a big issue for LLVM.
  static time_t num = ::time(0);
  char pathname[MAXPATHLEN];
  do {
    num++;
    sprintf(pathname, "/tmp/llvm_%010u", unsigned(num));
  } while ( 0 == access(pathname, F_OK ) );
  if (-1 == ::mkdir(pathname, S_IRWXU))
    ThrowErrno(std::string(pathname) + ": Can't create temporary directory");
  Path result;
  result.setDirectory(pathname);
  assert(result.isValid() && "mkstemp didn't create a valid pathname!");
  return result;
#endif
}

static void getPathList(const char*path, std::vector<sys::Path>& Paths) {
  const char* at = path;
  const char* delim = strchr(at, ':');
  Path tmpPath;
  while( delim != 0 ) {
    std::string tmp(at, size_t(delim-at));
    if (tmpPath.setDirectory(tmp))
      if (tmpPath.readable())
        Paths.push_back(tmpPath);
    at = delim + 1;
    delim = strchr(at, ':');
  }
  if (*at != 0)
    if (tmpPath.setDirectory(std::string(at)))
      if (tmpPath.readable())
        Paths.push_back(tmpPath);

}

void 
Path::GetSystemLibraryPaths(std::vector<sys::Path>& Paths) {
#ifdef LTDL_SHLIBPATH_VAR
  char* env_var = getenv(LTDL_SHLIBPATH_VAR);
  if (env_var != 0) {
    getPathList(env_var,Paths);
  }
#endif
  // FIXME: Should this look at LD_LIBRARY_PATH too?
  Paths.push_back(sys::Path("/usr/local/lib/"));
  Paths.push_back(sys::Path("/usr/X11R6/lib/"));
  Paths.push_back(sys::Path("/usr/lib/"));
  Paths.push_back(sys::Path("/lib/"));
}

void
Path::GetBytecodeLibraryPaths(std::vector<sys::Path>& Paths) {
  char * env_var = getenv("LLVM_LIB_SEARCH_PATH");
  if (env_var != 0) {
    getPathList(env_var,Paths);
  }
#ifdef LLVM_LIBDIR
  {
    Path tmpPath;
    if (tmpPath.setDirectory(LLVM_LIBDIR))
      if (tmpPath.readable())
        Paths.push_back(tmpPath);
  }
#endif
  GetSystemLibraryPaths(Paths);
}

Path 
Path::GetLLVMDefaultConfigDir() {
  return Path("/etc/llvm/");
}

Path
Path::GetUserHomeDirectory() {
  const char* home = getenv("HOME");
  if (home) {
    Path result;
    if (result.setDirectory(home))
      return result;
  }
  return GetRootDirectory();
}

bool
Path::isFile() const {
  return (isValid() && path[path.length()-1] != '/');
}

bool
Path::isDirectory() const {
  return (isValid() && path[path.length()-1] == '/');
}

std::string
Path::getBasename() const {
  // Find the last slash
  size_t slash = path.rfind('/');
  if (slash == std::string::npos)
    slash = 0;
  else
    slash++;

  return path.substr(slash, path.rfind('.'));
}

bool Path::hasMagicNumber(const std::string &Magic) const {
  size_t len = Magic.size();
  assert(len < 1024 && "Request for magic string too long");
  char* buf = (char*) alloca(1 + len);
  int fd = ::open(path.c_str(),O_RDONLY);
  if (fd < 0)
    return false;
  size_t read_len = ::read(fd, buf, len);
  close(fd);
  if (len != read_len)
    return false;
  buf[len] = '\0';
  return Magic == buf;
}

bool Path::getMagicNumber(std::string& Magic, unsigned len) const {
  if (!isFile())
    return false;
  assert(len < 1024 && "Request for magic string too long");
  char* buf = (char*) alloca(1 + len);
  int fd = ::open(path.c_str(),O_RDONLY);
  if (fd < 0)
    return false;
  ssize_t bytes_read = ::read(fd, buf, len);
  ::close(fd);
  if (ssize_t(len) != bytes_read) {
    Magic.clear();
    return false;
  }
  Magic.assign(buf,len);
  return true;
}

bool 
Path::isBytecodeFile() const {
  char buffer[ 4];
  buffer[0] = 0;
  int fd = ::open(path.c_str(),O_RDONLY);
  if (fd < 0)
    return false;
  ssize_t bytes_read = ::read(fd, buffer, 4);
  ::close(fd);
  if (4 != bytes_read) 
    return false;

  return (buffer[0] == 'l' && buffer[1] == 'l' && buffer[2] == 'v' &&
      (buffer[3] == 'c' || buffer[3] == 'm'));
}

bool
Path::exists() const {
  return 0 == access(path.c_str(), F_OK );
}

bool
Path::readable() const {
  return 0 == access(path.c_str(), F_OK | R_OK );
}

bool
Path::writable() const {
  return 0 == access(path.c_str(), F_OK | W_OK );
}

bool
Path::executable() const {
  return 0 == access(path.c_str(), R_OK | X_OK );
}

std::string 
Path::getLast() const {
  // Find the last slash
  size_t pos = path.rfind('/');

  // Handle the corner cases
  if (pos == std::string::npos)
    return path;

  // If the last character is a slash
  if (pos == path.length()-1) {
    // Find the second to last slash
    size_t pos2 = path.rfind('/', pos-1);
    if (pos2 == std::string::npos)
      return path.substr(0,pos);
    else
      return path.substr(pos2+1,pos-pos2-1);
  }
  // Return everything after the last slash
  return path.substr(pos+1);
}

void
Path::getStatusInfo(StatusInfo& info) const {
  struct stat buf;
  if (0 != stat(path.c_str(), &buf)) {
    ThrowErrno(std::string("Can't get status: ")+path);
  }
  info.fileSize = buf.st_size;
  info.modTime.fromEpochTime(buf.st_mtime);
  info.mode = buf.st_mode;
  info.user = buf.st_uid;
  info.group = buf.st_gid;
  info.isDir = S_ISDIR(buf.st_mode);
  if (info.isDir && path[path.length()-1] != '/')
    path += '/';
}

static bool AddPermissionBits(const std::string& Filename, int bits) {
  // Get the umask value from the operating system.  We want to use it
  // when changing the file's permissions. Since calling umask() sets
  // the umask and returns its old value, we must call it a second
  // time to reset it to the user's preference.
  int mask = umask(0777); // The arg. to umask is arbitrary.
  umask(mask);            // Restore the umask.

  // Get the file's current mode.
  struct stat st;
  if ((stat(Filename.c_str(), &st)) == -1)
    return false;

  // Change the file to have whichever permissions bits from 'bits'
  // that the umask would not disable.
  if ((chmod(Filename.c_str(), (st.st_mode | (bits & ~mask)))) == -1)
    return false;

  return true;
}

void Path::makeReadable() {
  if (!AddPermissionBits(path,0444))
    ThrowErrno(path + ": can't make file readable");
}

void Path::makeWriteable() {
  if (!AddPermissionBits(path,0222))
    ThrowErrno(path + ": can't make file writable");
}

void Path::makeExecutable() {
  if (!AddPermissionBits(path,0111))
    ThrowErrno(path + ": can't make file executable");
}

bool
Path::getDirectoryContents(std::set<Path>& result) const {
  if (!isDirectory())
    return false;
  DIR* direntries = ::opendir(path.c_str());
  if (direntries == 0)
    ThrowErrno(path + ": can't open directory");

  result.clear();
  struct dirent* de = ::readdir(direntries);
  while (de != 0) {
    if (de->d_name[0] != '.') {
      Path aPath(path + (const char*)de->d_name);
      struct stat buf;
      if (0 != stat(aPath.path.c_str(), &buf))
        ThrowErrno(aPath.path + ": can't get status");
      if (S_ISDIR(buf.st_mode))
        aPath.path += "/";
      result.insert(aPath);
    }
    de = ::readdir(direntries);
  }
  
  closedir(direntries);
  return true;
}

bool
Path::setDirectory(const std::string& a_path) {
  if (a_path.size() == 0)
    return false;
  Path save(*this);
  path = a_path;
  size_t last = a_path.size() -1;
  if (a_path[last] != '/')
    path += '/';
  if (!isValid()) {
    path = save.path;
    return false;
  }
  return true;
}

bool
Path::setFile(const std::string& a_path) {
  if (a_path.size() == 0)
    return false;
  Path save(*this);
  path = a_path;
  size_t last = a_path.size() - 1;
  while (last > 0 && a_path[last] == '/')
    last--;
  path.erase(last+1);
  if (!isValid()) {
    path = save.path;
    return false;
  }
  return true;
}

bool
Path::appendDirectory(const std::string& dir) {
  if (isFile()) 
    return false;
  Path save(*this);
  path += dir;
  path += "/";
  if (!isValid()) {
    path = save.path;
    return false;
  }
  return true;
}

bool
Path::elideDirectory() {
  if (isFile()) 
    return false;
  size_t slashpos = path.rfind('/',path.size());
  if (slashpos == 0 || slashpos == std::string::npos)
    return false;
  if (slashpos == path.size() - 1)
    slashpos = path.rfind('/',slashpos-1);
  if (slashpos == std::string::npos)
    return false;
  path.erase(slashpos);
  return true;
}

bool
Path::appendFile(const std::string& file) {
  if (!isDirectory()) 
    return false;
  Path save(*this);
  path += file;
  if (!isValid()) {
    path = save.path;
    return false;
  }
  return true;
}

bool
Path::elideFile() {
  if (isDirectory()) 
    return false;
  size_t slashpos = path.rfind('/',path.size());
  if (slashpos == std::string::npos)
    return false;
  path.erase(slashpos+1);
  return true;
}

bool
Path::appendSuffix(const std::string& suffix) {
  if (isDirectory()) 
    return false;
  Path save(*this);
  path.append(".");
  path.append(suffix);
  if (!isValid()) {
    path = save.path;
    return false;
  }
  return true;
}

bool 
Path::elideSuffix() {
  if (isDirectory()) return false;
  size_t dotpos = path.rfind('.',path.size());
  size_t slashpos = path.rfind('/',path.size());
  if (slashpos != std::string::npos && dotpos != std::string::npos &&
      dotpos > slashpos) {
    path.erase(dotpos, path.size()-dotpos);
    return true;
  }
  return false;
}


bool
Path::createDirectory( bool create_parents) {
  // Make sure we're dealing with a directory
  if (!isDirectory()) return false;

  // Get a writeable copy of the path name
  char pathname[MAXPATHLEN];
  path.copy(pathname,MAXPATHLEN);

  // Null-terminate the last component
  int lastchar = path.length() - 1 ; 
  if (pathname[lastchar] == '/') 
    pathname[lastchar] = 0;
  else 
    pathname[lastchar+1] = 0;

  // If we're supposed to create intermediate directories
  if ( create_parents ) {
    // Find the end of the initial name component
    char * next = strchr(pathname,'/');
    if ( pathname[0] == '/') 
      next = strchr(&pathname[1],'/');

    // Loop through the directory components until we're done 
    while ( next != 0 ) {
      *next = 0;
      if (0 != access(pathname, F_OK | R_OK | W_OK))
        if (0 != mkdir(pathname, S_IRWXU | S_IRWXG))
          ThrowErrno(std::string(pathname) + ": Can't create directory");
      char* save = next;
      next = strchr(next+1,'/');
      *save = '/';
    }
  } 

  if (0 != access(pathname, F_OK | R_OK))
    if (0 != mkdir(pathname, S_IRWXU | S_IRWXG))
      ThrowErrno(std::string(pathname) + ": Can't create directory");
  return true;
}

bool
Path::createFile() {
  // Make sure we're dealing with a file
  if (!isFile()) return false; 

  // Create the file
  int fd = ::creat(path.c_str(), S_IRUSR | S_IWUSR);
  if (fd < 0)
    ThrowErrno(path + ": Can't create file");
  ::close(fd);

  return true;
}

bool
Path::createTemporaryFile(bool reuse_current) {
  // Make sure we're dealing with a file
  if (!isFile()) 
    return false;

  // Make this into a unique file name
  makeUnique( reuse_current );

  // create the file
  int outFile = ::open(path.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666);
  if (outFile != -1) {
    ::close(outFile);
    return true;
  }
  return false;
}

bool
Path::destroyDirectory(bool remove_contents) const {
  // Make sure we're dealing with a directory
  if (!isDirectory()) return false;

  // If it doesn't exist, we're done.
  if (!exists()) return true;

  if (remove_contents) {
    // Recursively descend the directory to remove its content
    std::string cmd("/bin/rm -rf ");
    cmd += path;
    system(cmd.c_str());
  } else {
    // Otherwise, try to just remove the one directory
    char pathname[MAXPATHLEN];
    path.copy(pathname,MAXPATHLEN);
    int lastchar = path.length() - 1 ; 
    if (pathname[lastchar] == '/') 
      pathname[lastchar] = 0;
    else
      pathname[lastchar+1] = 0;
    if ( 0 != rmdir(pathname))
      ThrowErrno(std::string(pathname) + ": Can't destroy directory");
  }
  return true;
}

bool
Path::destroyFile() const {
  if (!isFile()) return false;
  if (0 != unlink(path.c_str()))
    ThrowErrno(path + ": Can't destroy file");
  return true;
}

bool
Path::renameFile(const Path& newName) {
  if (!isFile()) return false;
  if (0 != rename(path.c_str(), newName.c_str()))
    ThrowErrno(std::string("can't rename ") + path + " as " + 
               newName.toString());
  return true;
}

bool
Path::setStatusInfo(const StatusInfo& si) const {
  if (!isFile()) return false;
  struct utimbuf utb;
  utb.actime = si.modTime.toPosixTime();
  utb.modtime = utb.actime;
  if (0 != ::utime(path.c_str(),&utb))
    ThrowErrno(path + ": can't set file modification time");
  if (0 != ::chmod(path.c_str(),si.mode))
    ThrowErrno(path + ": can't set mode");
  return true;
}

void 
sys::CopyFile(const sys::Path &Dest, const sys::Path &Src) {
  int inFile = -1;
  int outFile = -1;
  try {
    inFile = ::open(Src.c_str(), O_RDONLY);
    if (inFile == -1)
      ThrowErrno("Cannnot open source file to copy: " + Src.toString());

    outFile = ::open(Dest.c_str(), O_WRONLY|O_CREAT, 0666);
    if (outFile == -1)
      ThrowErrno("Cannnot create destination file for copy: " +Dest.toString());

    char Buffer[16*1024];
    while (ssize_t Amt = ::read(inFile, Buffer, 16*1024)) {
      if (Amt == -1) {
        if (errno != EINTR && errno != EAGAIN) 
          ThrowErrno("Can't read source file: " + Src.toString());
      } else {
        char *BufPtr = Buffer;
        while (Amt) {
          ssize_t AmtWritten = ::write(outFile, BufPtr, Amt);
          if (AmtWritten == -1) {
            if (errno != EINTR && errno != EAGAIN) 
              ThrowErrno("Can't write destination file: " + Dest.toString());
          } else {
            Amt -= AmtWritten;
            BufPtr += AmtWritten;
          }
        }
      }
    }
    ::close(inFile);
    ::close(outFile);
  } catch (...) {
    if (inFile != -1)
      ::close(inFile);
    if (outFile != -1)
      ::close(outFile);
    throw;
  }
}

void 
Path::makeUnique(bool reuse_current) {
  if (reuse_current && !exists())
    return; // File doesn't exist already, just use it!

  // Append an XXXXXX pattern to the end of the file for use with mkstemp, 
  // mktemp or our own implementation.
  char *FNBuffer = (char*) alloca(path.size()+8);
  path.copy(FNBuffer,path.size());
  strcpy(FNBuffer+path.size(), "-XXXXXX");

#if defined(HAVE_MKSTEMP)
  int TempFD;
  if ((TempFD = mkstemp(FNBuffer)) == -1) {
    ThrowErrno("Cannot make unique filename for '" + path + "'");
  }

  // We don't need to hold the temp file descriptor... we will trust that no one
  // will overwrite/delete the file before we can open it again.
  close(TempFD);

  // Save the name
  path = FNBuffer;
#elif defined(HAVE_MKTEMP)
  // If we don't have mkstemp, use the old and obsolete mktemp function.
  if (mktemp(FNBuffer) == 0) {
    ThrowErrno("Cannot make unique filename for '" + path + "'");
  }

  // Save the name
  path = FNBuffer;
#else
  // Okay, looks like we have to do it all by our lonesome.
  static unsigned FCounter = 0;
  unsigned offset = path.size() + 1;
  while ( FCounter < 999999 && exists()) {
    sprintf(FNBuffer+offset,"%06u",++FCounter);
    path = FNBuffer;
  }
  if (FCounter > 999999)
    throw std::string("Cannot make unique filename for '" + path + "'");
#endif

}
}

// vim: sw=2
