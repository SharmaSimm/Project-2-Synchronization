shm_proc: shm_processes.c
	gcc shm_processes.c -D_SVID_SOURCE -pthread -std=c99 -lpthread  -o shm_proc
example: example.c
	gcc example.c -pthread -std=c99 -lpthread  -o example

psdd: psdd.c
	@gcc psdd.c -pthread -std=c99 -Wall -Wextra -pedantic -o psdd
	@echo "Built psdd"

run-psdd: psdd
	./psdd


psdd_ec: psdd_ec.c
	@gcc psdd_ec.c -pthread -std=c99 -Wall -Wextra -pedantic -o psdd_ec
	@echo "Built psdd_ec"

run-ec-d1s3: psdd_ec
	./psdd_ec 1 3

run-ec-d2s10: psdd_ec
	./psdd_ec 2 10
