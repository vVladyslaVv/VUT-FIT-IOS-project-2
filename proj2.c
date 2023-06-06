#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>

// File
FILE *pfile;

// Fr log
#define LOG(...)                 \
    fprintf(pfile, __VA_ARGS__); \
    fflush(pfile);

// Call mmap with error hadling
void *make_mmap(size_t size)
{
    void *res = NULL;
    if ((res = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
    {
        LOG("Error: make_mmap failed.\n");
        exit(EXIT_FAILURE);
    }

    return res;
}

// Call mummap with error hadling
void make_munmap(void *ptr, size_t size)
{
    if (munmap(ptr, size) == -1)
    {
        LOG("Error: make_munmap failed.\n");
        exit(EXIT_FAILURE);
    }
}

// Creates sem
sem_t *make_sem()
{
    sem_t *res = NULL;
    if ((res = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
    {
        LOG("Error: make_sem failed.\n");
        exit(EXIT_FAILURE);
    }

    if (sem_init(res, 1, 0) == -1)
    {
        LOG("Error: make_sem failed\n");
        exit(EXIT_FAILURE);
    }

    return res;
}

// Destroy sem
void make_destroy_sem(sem_t *sem)
{
    if (munmap(sem, sizeof(*sem)) == -1)
    {
        LOG("Error: destroy_sem failed.\n");
        exit(EXIT_FAILURE);
    }

    if (sem_destroy(sem) == -1)
    {
        LOG("Error: destroy_sem failed.\n");
        exit(EXIT_FAILURE);
    }
}

// Out filename
#define FILE_NAME "proj2.out"

// Shared data
int *is_closed;
int *line;
int *line_count;

// Prints all lines
void dump_lines()
{
    for (size_t i = 0; i < 3; i++)
    {
        LOG("%d, ", line[i]);
    }
    LOG("\n");
}

// free all lines
bool all_line_free()
{
    for (size_t i = 0; i < 3; i++)
    {
        if (line[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

// Returns random line witch is not 0
int rand_line()
{
    int val = rand() % 3;

    while (1)
    {
        if (line[val] == 0)
        {
            val = rand() % 3;
        }
        else
            break;
    }

    int res = val;
    return res;
}

// Shared memory semaphores
sem_t *mutex = NULL;
sem_t **post_sem = NULL;

// Function prototypes
void customer_process(int idZ, int TZ);
void clerk_process(int idU, int TU);
void main_process(int NZ, int NU, int TZ, int TU, int F);

int main(int argc, char *argv[])
{
    srand(time(NULL));

    // Parse and validate command-line arguments
    if (argc != 6)
    {
        fprintf(stderr, "Error: Invalid number of arguments\n");
        return 1;
    }

    // Parse and assign command-line arguments
    int NZ = atoi(argv[1]);
    int NU = atoi(argv[2]);
    int TZ = atoi(argv[3]);
    int TU = atoi(argv[4]);
    int F = atoi(argv[5]);

    // Initialize file
    if ((pfile = fopen(FILE_NAME, "w")) == NULL)
    {
        fprintf(stderr, "Error: file opening failed.\n");
        exit(EXIT_FAILURE);
    }

    // Initialize shared variables
    is_closed = make_mmap(sizeof(int));
    *is_closed = 0;

    line_count = make_mmap(sizeof(int));
    (*line_count)= 1;

    line = make_mmap(sizeof(int) * 3);
    for (int i = 0; i < 3; i++)
    {
        line[i] = 0;
    }

    // Initialize sems
    mutex = make_sem();
    sem_post(mutex);

    post_sem = make_mmap(sizeof(sem_t *) * 3);
    for (int i = 0; i < 3; i++)
    {
        post_sem[i] = make_sem();
    }

    // Run the main process
    main_process(NZ, NU, TZ, TU, F);

    // Cleanup
    make_munmap(line_count, sizeof(int));

    make_munmap(is_closed, sizeof(int));

    make_munmap(line, sizeof(sizeof(int) * 3));

    make_destroy_sem(mutex);

    for (int i = 0; i < 3; i++)
    {
        make_destroy_sem(post_sem[i]);
    }

    make_munmap(post_sem, sizeof(sem_t *) * 3);

    // Close file
    if (fclose(pfile) == EOF)
    {
        fprintf(stderr, "Error: file closing failed.\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

void customer_process(int idZ, int TZ)
{
    srand(getpid() * time(NULL));

    // Starting
    fprintf(pfile, "%d: Z %d: started\n", (*line_count)++, idZ + 1);
    fflush(pfile);
    usleep(rand() % (TZ + 1));

    // Should go home?
    sem_wait(mutex);
    if ((*is_closed))
    {
        fprintf(pfile, "%d: Z %d: going home\n", (*line_count)++, idZ + 1);
        fflush(pfile);
        sem_post(mutex);
        exit(EXIT_SUCCESS);
    }

    // Select service type and join queue
    int service_type = rand() % 3;
    fprintf(pfile, "%d: Z %d: entering office for a service %d\n", (*line_count)++, idZ + 1, service_type + 1);
    fflush(pfile);
    line[service_type]++;
    sem_post(mutex);

    // Wait for response
    sem_wait(post_sem[service_type]);
    sem_wait(mutex);

    fprintf(pfile, "%d: Z %d: called by office worker\n", (*line_count)++, idZ + 1);
    fflush(pfile);

    // Just waiting
    usleep((rand() % TZ));

    // Customer is done and going home
    fprintf(pfile, "%d: Z %d: going home\n", (*line_count)++, idZ + 1);
    fflush(pfile);

    sem_post(mutex);
    exit(EXIT_SUCCESS);
}

void clerk_process(int idU, int TU)
{
    // Start
    srand(getpid() * time(NULL));
    fprintf(pfile, "%d: U %d: started\n", (*line_count)++, idU + 1);
    fflush(pfile);

    while (1)
    {
        sem_wait(mutex);
        // SHould go home?
        if (*is_closed && all_line_free())
        {
            fprintf(pfile, "%d: U %d: going home\n", (*line_count)++, idU + 1);
            fflush(pfile);
            sem_post(mutex);
            exit(EXIT_SUCCESS);
        }

        // Should take brake?
        if (all_line_free())
        {
            fprintf(pfile, "%d: U %d: taking break\n", (*line_count)++, idU + 1);
            fflush(pfile);
            sem_post(mutex);
            usleep(rand() % TU);
            sem_wait(mutex);
            fprintf(pfile, "%d: U %d: break finished\n", (*line_count)++, idU + 1);
            fflush(pfile);
            sem_post(mutex);
            continue;
        }

        // Select random customer
        int line_n = rand_line();
        line[line_n]--;


        fprintf(pfile, "%d: U %d: serving a service of type %d\n", (*line_count)++, idU + 1, line_n + 1);
        fflush(pfile);

        sem_post(post_sem[line_n]);

        // Wait
        usleep(rand() % 10);

        // Finished service
        fprintf(pfile, "%d: U %d: service finished\n", (*line_count)++, idU + 1);
        fflush(pfile);
        sem_post(mutex);
    }

    exit(EXIT_SUCCESS);
}

void main_process(int NZ, int NU, int TZ, int TU, int F)
{

    // Create customer and clerk processes
    for (int i = 0; i < NZ; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            customer_process(i, TZ);
            exit(0);
        }
    }

    for (int i = 0; i < NU; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            clerk_process(i, TU);
            exit(0);
        }
    }

    // Wait for a random time between F/2 and F
    usleep((rand() % (F / 2) + F / 2));

    fprintf(pfile, "%d: closing\n", (*line_count)++);
    fflush(pfile);

    // Close the post office
    sem_wait(mutex);
    (*is_closed) = 1;
    sem_post(mutex);

    // Wait for all child processes to terminate
    pid_t wpid;
    int status = 0;
    while ((wpid = wait(&status)) > 0)
        ;
}