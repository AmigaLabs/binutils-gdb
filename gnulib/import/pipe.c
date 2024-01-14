/* Create a pipe.
   Copyright (C) 2009-2022 Free Software Foundation, Inc.

   This file is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   This file is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#include <config.h>

/* Specification.  */
#include <unistd.h>

#if defined _WIN32 && ! defined __CYGWIN__
/* Native Windows API.  */

/* Get _pipe().  */
# include <io.h>

/* Get _O_BINARY.  */
# include <fcntl.h>

int
pipe (int fd[2])
{
  /* Mingw changes fd to {-1,-1} on failure, but this violates
     http://austingroupbugs.net/view.php?id=467 */
  int tmp[2];
  int result = _pipe (tmp, 4096, _O_BINARY);
  if (!result)
    {
      fd[0] = tmp[0];
      fd[1] = tmp[1];
    }
  return result;
}
# elif defined(__amigaos4__) && defined(__NEWLIB__)							/* AMIGAOS PPC */
#include <proto/dos.h>
#include <proto/exec.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

int pipenum = 0;

int pipe(int fd[2]) {
    char pipe_name[1024] = {0};

    snprintf(pipe_name, sizeof(pipe_name), "PIPE:%x%lu/32768/0", pipenum++, ((struct Process *) IExec->FindTask(NULL))->pr_ProcessID);

    fd[1] = open(pipe_name, O_WRONLY | O_CREAT);
    fd[0] = open(pipe_name, O_RDONLY);

    if (fd[0] == -1 || fd[1] == -1) {
        if (fd[0] != -1)
            close(fd[0]);
        if (fd[1] != -1)
            close(fd[1]);

        errno = EINVAL;

        return -1;
    }

    return 0;
}
#else

# error "This platform lacks a pipe function, and Gnulib doesn't provide a replacement. This is a bug in Gnulib."

#endif
