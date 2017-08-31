#include "event.h"
#include "options.h"
#include "upalight.h"

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

/* list of actions

  handle options
  start server
  start event listening

  for event in events
    if event == error
      perror and quit
    if event == EOF
      remove client
    if event == server
      add client
    if event != client
      handle client
    end if
  end for

end of action list */

int
main(int argc, char** argv)
{
  signal(SIGTERM, evclean);
  signal(SIGINT, evclean);

  struct options opts;
  int ret = parse(argc, argv, &opts);
  if (ret == -1) {
    return EXIT_FAILURE;
  }

  ret = startsvr(opts.address, opts.port);
  if (ret == -1) {
    return EXIT_FAILURE;
  }
  printf("server started\n");

  ret = initevl();
  if (ret == -1) {
    return EXIT_FAILURE;
  }
  printf("event listening started\n");

  ret = startevl();
  if (ret == -1) {
    return EXIT_FAILURE;
  }
  printf("event listening ended\n");

  return 0;
}
