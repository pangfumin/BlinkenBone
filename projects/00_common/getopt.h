
#ifndef GETOPT_H_
#define GETOPT_H_

#ifndef GETOPT_C_
extern int	opterr;
extern int	optind;
extern int	optopt;
extern char	*optarg;
#endif

int getopt(int argc, char **argv, char *opts) ; 


#endif