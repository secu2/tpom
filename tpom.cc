#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <string>

using std::string;
const int kDefaultLengthSecs = 25 * 60;
const char* kDefaultDoneMessage = "";

string SocketName() {
  string socket_name;
  socket_name.append(P_tmpdir);
  socket_name.append("/.tpom-");
  socket_name.append(getenv("USER"));
  return socket_name;
}

string MakePostHookPath() {
  string post_hook;
  post_hook = getenv("HOME");
  post_hook += "/.tpom-post.sh";
  return post_hook;
}

int ClientMain(string done_message) {
  int sock_fd;
  struct sockaddr_un remote;
  if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }
  remote.sun_family = AF_UNIX;
  string socket_name = SocketName();
  strcpy(remote.sun_path, socket_name.c_str());

  int len = strlen(remote.sun_path) + sizeof(remote.sun_family) + 1;
  if (connect(sock_fd, (struct sockaddr *)&remote, len) == -1) {
    printf("%s\n", done_message.c_str());
    return 0;
  }

  int recv_length;
  char buf[100];
  if ((recv_length = recv(sock_fd, buf, 100, 0)) > 0) {
    buf[recv_length] = '\0';
    printf("%s\n", buf);
  }
  close(sock_fd);
  return 0;
}

int DaemonMain(int countdown_time) {
  /* Our process ID and Session ID */
  int ok;
  pid_t pid, sid;
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 1000 /*micro*/ * 1000 /* milli */ * 50;

  string socket_name = SocketName();

  int sock_fd;
  struct sockaddr_un local, remote;

  if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }
  local.sun_family = AF_UNIX;
  strcpy(local.sun_path, socket_name.c_str());
  unlink(local.sun_path);
  int len = strlen(local.sun_path) + sizeof(local.sun_family) + 1;
  if (bind(sock_fd, (struct sockaddr *)&local, len) == -1) {
    perror("bind");
    exit(1);
  }
  int flags = fcntl(sock_fd,F_GETFL,0);
  fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then
     we can exit the parent process. */
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  /* Change the file mode mask */
  umask(0);

  /* Open any logs here */

  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
    /* Log the failure */
    exit(EXIT_FAILURE);
  }


  /* Change the current working directory */
  if ((chdir("/")) < 0) {
    /* Log the failure */
    exit(EXIT_FAILURE);
  }

  /* Close out the standard file descriptors */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  if (listen(sock_fd, 5) == -1) {
    exit(1);
  }


  struct timeval start_time;
  ok = gettimeofday(&start_time, NULL);
  int remote_fd = -1;

  bool wakeup = false;
  while (!wakeup) {
    struct timeval current_time;
    struct timeval diff_time;
    ok = gettimeofday(&current_time, NULL);
    timersub(&current_time, &start_time, &diff_time);
    if (diff_time.tv_sec >= countdown_time) {
      wakeup = true;
      continue;
    }
    long remaining_time = (long)countdown_time - diff_time.tv_sec;
    socklen_t t = sizeof(remote);
    while ((remote_fd = accept(sock_fd, (struct sockaddr *)&remote, &t)) != -1) {
      char message[100];
      int message_len = snprintf(message, 100, "%ld:%02ld", remaining_time / 60, remaining_time % 60);
      if (send(remote_fd, message, message_len, 0) < 0) {
        exit(1);
      }
      close(remote_fd);
    }
    // Make sure it was just EWOULDBLOCK
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      exit(1);
    }
    ok = nanosleep(&ts, NULL);
  }
  close(sock_fd);
  printf("%s\n", local.sun_path);
  unlink(local.sun_path);
  string post_hook = MakePostHookPath();
  char* argv[] = {(char *)post_hook.c_str(), NULL};
  if (access(post_hook.c_str(), X_OK) != -1) {
    execv(post_hook.c_str(), argv);
  }
  exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
  int c;
  int countdown_time = kDefaultLengthSecs;
  string done_message = kDefaultDoneMessage;
  int got_positional = 0;
  string command = "";
  while (1) {
    while ( (c = getopt(argc, argv, "bs:m:d:")) != -1) {
      switch (c) {
        case 's':
          countdown_time = atoi(optarg);
          break;
        case 'm':
          countdown_time = atoi(optarg) * 60;
          break;
        case 'd':
          done_message = optarg;
          break;
      }
    }
    if (optind < argc && !got_positional) {
      command = argv[optind];
      got_positional = 1;
    } else {
      break;
    }
    optind++;
  }


  if (command == "start") {
    DaemonMain(countdown_time);
  }
  else {
    return ClientMain(done_message);
  }
  exit(0);
}
