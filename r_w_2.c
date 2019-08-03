/*!
 * @file
 * Readers and Writers - implementation 2
 *
 * Implementation with no starvation of writers or readers. It uses conditional variables - one for each reader and
 * one for each writer. In this implementation there is an additional thread - librarian. All readers and writers wait
 * in FIFO queue. Librarian checks every second who is at first position at queue - if it's reader librarian waits till
 * no writer is in library (if there was any). Then it sends signal to reader that he can come in. If there is a writer
 * at first position in queue - librarian waits till everyone leave library and then sends signal to the writer.
 *
 * @author Mateusz Wawreszuk
 */

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"
#pragma ide diagnostic ignored "OCSimplifyInspection"
#pragma ide diagnostic ignored "cert-msc30-c"
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#pragma ide diagnostic ignored "cert-msc32-c"
#pragma ide diagnostic ignored "cert-err34-c"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>

/*!
 * @brief Wrong arguments error message.
 */
#define ERROR_ARGUMENTS_MESSAGE "Usage: ReaderAndWriters2 number_of_writers number_of_readers [-t min_reading_time max_reading_time min_writing_time max_writing_time] [-debug]\n"

/*!
 * @brief Thread kind to mark there is no one at this position in queue or library.
 */
#define NO_KIND 0
/*!
 * @brief Reader thread kind.
 */
#define READER_KIND 1
/*!
 * @brief Reader thread kind.
 */
#define WRITER_KIND 2

/*!
 * @brief Thread presence in queue or in library.
 */
struct presence {
/*!
 * @brief thread kind (NO_KIND / READER_KIND / WRITER_KIND)
 */
    int kind;
/*!
 * @brief reader or writer id
 */
    int id;
/*!
 * @brief when thread get to this position in queue or to library
 */
    time_t timestamp;
};

void print();
void* reader(void* arg);
void* writer(void* arg);
void* librarian();
void write_book(int writer_id);
void read_books(int reader_id);
int get_random(int min, int max);
time_t get_timestamp();
void init_queue();
void signal_handler();
void args_interpreter(int argc, char **argv);
void variables_initializer();
void cleaner();
int get_readers_queue_count();
int get_writers_queue_count();
int get_writers_in_library_count();
int get_readers_in_library_count();
void get_to_queue(int kind, int id);
void leave_queue();
void get_to_library(int kind, int id);
void leave_library(int kind, int id);

/*!
 * @brief Loop in main function works until signal_flag is set. When SIGINT signal is received, flag is changed to 0 and
 * loop stops.
 */
volatile int signal_flag = 1;

/*!
 * @brief Just a mutex.
 */
pthread_mutex_t mutex;
/*!
 * @brief Array of conditional variables to handle readers.
 */
pthread_cond_t *readers_conds;
/*!
 * @brief Array of conditional variables to handle writers.
 */
pthread_cond_t *writers_conds;

/*!
 * @brief Flag marking debug mode.
 */
int is_debug_run = 0;

/*!
 * @brief Number of readers.
 */
int readers_count;
/*!
 * @brief Number of writers
 */
int writers_count;

/*!
 * @brief Common writers and readers queue.
 */
struct presence *queue;
/*!
 * @brief Presence in library - common for writers and readers.
 */
struct presence *in_library;

/*!
 * @brief Minimum time that reader spends in library.
 */
int min_reading_time = 0;
/*!
 * @brief Maximum time that reader spends in library.
 */
int max_reading_time = 5;
/*!
 * @brief Minimum time that writer spends in library.
 */
int min_writing_time = 5;
/*!
 * @brief Maximum time that writer spends in library.
 */
int max_writing_time = 15;

/*!
 * @brief Creates readers, writers and librarian threads. After that function works in endless loop and (in debug mode)
 * prints library state every second. Loop can be stopped with SIGINT signal (ctrl+c). In that case function cancels all
 * threads and frees memory allocated for variables.
 *
 * @param argc arguments count
 * @param argv arguments array
 * @return 0 (no error code)
 */
int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    args_interpreter(argc, argv);
    variables_initializer();

    int *reader_ids = malloc(readers_count * sizeof(int));
    int *writer_ids = malloc(writers_count * sizeof(int));
    pthread_t *readers = malloc(readers_count * sizeof(pthread_t));
    pthread_t *writers = malloc(writers_count * sizeof(pthread_t));
    pthread_t librarian_t;

    init_queue();

    print();

    int i;

    for (i = 0;i < readers_count;i++) {
        pthread_cond_init(&readers_conds[i], NULL);
        reader_ids[i] = i;
        pthread_create(&readers[i], NULL, reader, (void *) &reader_ids[i]);
    }
    for (i = 0;i < writers_count;i++) {
        pthread_cond_init(&writers_conds[i], NULL);
        writer_ids[i] = i;
        pthread_create(&writers[i], NULL, writer, (void *) &writer_ids[i]);
    }
    pthread_create(&librarian_t, NULL, librarian, NULL);

    if (is_debug_run) {
        while (signal_flag) {
            sleep(1);
            pthread_mutex_lock(&mutex);
            print();
            pthread_mutex_unlock(&mutex);
        }
    } else {
        while (signal_flag);
    }
    printf("\nCleaning up...\n\n");

    for (i = 0;i < readers_count;i++) {
        pthread_cancel(readers[i]);
    }
    for (i = 0;i < writers_count;i++) {
        pthread_cancel(writers[i]);
    }
    pthread_cancel(librarian_t);

    cleaner();
    free(readers);
    free(reader_ids);
    free(writers);
    free(writer_ids);

    return 0;
}

/*!
 * @brief Function counts writers in queue.
 *
 * @return Number of writers in queue
 */
int get_writers_queue_count() {
    int count = 0;
    for (int i = 0;i < writers_count + readers_count;i++) {
        if (queue[i].kind == WRITER_KIND) {
            count++;
        }
    }
    return count;
}

/*!
 * @brief Function counts readers in queue.
 *
 * @return Number of readers in queue
 */
int get_readers_queue_count() {
    int count = 0;
    for (int i = 0;i < writers_count + readers_count;i++) {
        if (queue[i].kind == READER_KIND) {
            count++;
        }
    }
    return count;
}

/*!
 * @brief Function counts writers in library.
 *
 * @return Number of writers in library
 */
int get_writers_in_library_count() {
    int count = 0;
    for (int i = 0;i < writers_count + readers_count;i++) {
        if (in_library[i].kind == WRITER_KIND) {
            count++;
        }
    }
    return count;
}

/*!
 * @brief Function counts readers in library.
 *
 * @return Number of readers in library
 */
int get_readers_in_library_count() {
    int count = 0;
    for (int i = 0;i < writers_count + readers_count;i++) {
        if (in_library[i].kind == READER_KIND) {
            count++;
        }
    }
    return count;
}

/*!
 * @brief Prints library and queues state. There are two different of printing - one standard and one for debug mode.
 *
 * In standard mode function prints message in format:
 * ReaderQ: readers_in_queue WriterQ: writers_in_queue [ in: R:readers_in_library W:writers_in_library ]
 *
 * In debug mode function prints all threads with theirs numbers - grouped to queue and library.
 * Format:
 * Queue (seconds in queue):
 * Reader reader_number (seconds_in_queue) or Writer writer_number (seconds_in_queue)
 * (...)
 *
 * In library (seconds in library):
 * Writer writer_number (seconds_in_queue) or Reader reader_number (seconds_in_queue)
 * (...)
 */
void print() {
    if (is_debug_run == 0) {
        printf("ReaderQ: %i\t", get_readers_queue_count());
        printf("WriterQ: %i\t", get_writers_queue_count());
        printf("[ in: R:%i\t", get_readers_in_library_count());
        printf("W:%i ]\n", get_writers_in_library_count());
    } else {
        int i;
        for (i = 0;i < 100;i++) {
            printf("\n");
        }
        printf("Queue (seconds in queue):\n");
        for (i = 0;i < readers_count + writers_count;i++) {
            switch (queue[i].kind) {
                case WRITER_KIND:
                    printf("Writer %i\t", queue[i].id);
                    printf("(%li)\n", get_timestamp() - queue[i].timestamp);
                    break;
                case READER_KIND:
                    printf("Reader %i\t", queue[i].id);
                    printf("(%li)\n", get_timestamp() - queue[i].timestamp);
                    break;
                default:
                    break;
            }
        }
        printf("\nIn library (seconds in library):\n");
        for (i = 0;i < readers_count + writers_count;i++) {
            switch (in_library[i].kind) {
                case WRITER_KIND:
                    printf("Writer %i\t", in_library[i].id);
                    printf("(%li)\n", get_timestamp() - in_library[i].timestamp);
                    break;
                case READER_KIND:
                    printf("Reader %i\t", in_library[i].id);
                    printf("(%li)\n", get_timestamp() - in_library[i].timestamp);
                    break;
                default:
                    break;
            }
        }
    }
}

/*!
 * @brief Readers thread. It works in endless loop. It waits for signal from its conditional variable and then enters
 * library.
 *
 * @param arg Reader id
 */
void* reader(void* arg) {
    int reader_id = *((int *) arg);
    while (1) {
        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&readers_conds[reader_id], &mutex);
        pthread_mutex_unlock(&mutex);
        read_books(reader_id);
    }
}

/*!
 * @brief Writers thread. It works in endless loop. It waits for signal from its conditional variable and then enters
 * library.
 *
 * @param arg Writer id
 */
void* writer(void* arg) {
    int writer_id = *((int *) arg);
    while (1) {
        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&writers_conds[writer_id], &mutex);
        pthread_mutex_unlock(&mutex);
        write_book(writer_id);
    }
}

/*!
 * @brief Librarians thread. It checks every second who is at first position at queue - if it's reader, librarian waits
 * till there is no writer in library (if there was any). Then it sends signal to reader that he can come in. If there
 * is a writer at first position in queue - librarian waits till everyone leaves library and then sends signal to the
 * writer.
 */
void* librarian() {
    int temp1;
    int temp2;
    while (1) {
        switch (queue[0].kind) {
            case READER_KIND:
                do {
                    pthread_mutex_lock(&mutex);
                    temp1 = get_writers_in_library_count();
                    pthread_mutex_unlock(&mutex);
                } while (temp1);
                pthread_cond_broadcast(&readers_conds[queue[0].id]);
                break;
            case WRITER_KIND:
                do {
                    pthread_mutex_lock(&mutex);
                    temp1 = get_readers_in_library_count();
                    temp2 = get_writers_in_library_count();
                    pthread_mutex_unlock(&mutex);
                } while (temp1 || temp2);
                pthread_cond_broadcast(&writers_conds[queue[0].id]);
                break;
            default:
                do {
                    pthread_mutex_lock(&mutex);
                    temp1 = get_readers_in_library_count();
                    temp2 = get_writers_in_library_count();
                    pthread_mutex_unlock(&mutex);
                } while (temp1 || temp2);
                pthread_cond_broadcast(&readers_conds[queue[0].id]);
                break;
        }
        sleep(1);
    }
}

/*!
 * @brief Function looks for first empty position in queue and puts there writer or reader. It also sets current
 * timestamp.
 *
 * @param kind Kind of thread that want to get to queue
 * @param id Id of thread that want to get to queue
 */
void get_to_queue(int kind, int id) {
    time_t timestamp = get_timestamp();
    int first_empty_position = 0;
    while (queue[first_empty_position].kind != NO_KIND) {
        first_empty_position++;
    }
    queue[first_empty_position].kind = kind;
    queue[first_empty_position].id = id;
    queue[first_empty_position].timestamp = timestamp;
}

/*!
 * @brief Function looks for first empty position in *in_library array and puts there writer or reader. It also sets
 * current timestamp.
 *
 * @param kind Kind of thread that want to get to library
 * @param id Id of thread that want to get to library
 */
void get_to_library(int kind, int id) {
    time_t timestamp = get_timestamp();
    int first_empty_position = 0;
    while (in_library[first_empty_position].kind != NO_KIND) {
        first_empty_position++;
    }
    in_library[first_empty_position].kind = kind;
    in_library[first_empty_position].id = id;
    in_library[first_empty_position].timestamp = timestamp;
}

/*!
 * @brief Function puts at every position in queue presence from next position. Previous last position is overridden
 * with NO_KIND presence.
 */
void leave_queue() {
    int i = 0;
    while (queue[i + 1].kind != NO_KIND && i < 20) {
        queue[i].kind = queue[i + 1].kind;
        queue[i].id = queue[i + 1].id;
        queue[i].timestamp = queue[i + 1].timestamp;
        i++;
    }
    queue[i].kind = NO_KIND;
}

/*!
 * @brief Looks *in_library array for thread wanting to leave library and overrides it with NO_KIND presence.
 *
 * @param kind Kind of thread that wants to leave library
 * @param id Id of thread that wants to leave library
 */
void leave_library(int kind, int id) {
    for (int i = 0;i < readers_count + writers_count;i++) {
        if (in_library[i].kind == kind && in_library[i].id == id) {
            in_library[i].kind = NO_KIND;
            break;
        }
    }
}

/*!
 * @brief Function symbolises entering library by a writer and writing a book. It takes off first thread from queue
 * and puts writer to *in_library array. After that function sleeps for some random time (by default 5-15 seconds, it
 * can be changed by main function arguments) and leaves library (removes itself from *in_library array and gets back
 * to queue).
 *
 * @param writer_id Writer thread id
 */
void write_book(int writer_id) {
    pthread_mutex_lock( &mutex );

    leave_queue();
    get_to_library(WRITER_KIND, writer_id);

    print();

    pthread_mutex_unlock( &mutex );

    sleep(get_random(min_writing_time, max_writing_time));

    pthread_mutex_lock( &mutex );

    leave_library(WRITER_KIND, writer_id);
    get_to_queue(WRITER_KIND, writer_id);

    print();

    pthread_mutex_unlock( &mutex );
}

/*!
 * @brief Function symbolises entering library by a reader and reading books. It takes off first thread from queue
 * and puts reader to *in_library array. After that function sleeps for some random time (by default 0-5 seconds, it can
 * be changed by main function arguments) and leaves library (removes itself from *in_library array and gets back to
 * queue).
 *
 * @param reader_id Reader thread id
 */
void read_books(int reader_id) {
    pthread_mutex_lock( &mutex );
    leave_queue();
    get_to_library(READER_KIND, reader_id);

    print();

    pthread_mutex_unlock( &mutex );

    sleep(get_random(min_reading_time, max_reading_time));

    pthread_mutex_lock( &mutex );

    leave_library(READER_KIND, reader_id);
    get_to_queue(READER_KIND, reader_id);

    print();

    pthread_mutex_unlock( &mutex );
}

/*!
* @brief Function initialises *queue and *in_library arrays.
*/
void init_queue() {
    int i;
    for (i = 0;i < readers_count;i++) {
        get_to_queue(READER_KIND, i);
        in_library[i].kind = NO_KIND;
    }
    for (i = readers_count;i < writers_count + readers_count;i++) {
        get_to_queue(WRITER_KIND, i - readers_count);
        in_library[i].kind = NO_KIND;
    }
}

/*!
 * @brief Function gets timestamp from system time.
 *
 * @return Timestamp
 */
time_t get_timestamp() {
    return time(NULL);
}

/*!
 * @brief Function gets random integer from min - max range (inclusive).
 *
 * @param min Minimum number that can be generated.
 * @param max Maximum number that can be generated.
 * @return Random integer
 */
int get_random(int min, int max) {
    int range = max - min + 1;
    return (rand() % range) + min;
}

/*!
 * @brief Signal handler changes signal_flag to 0.
 */
void signal_handler() {
    signal_flag = 0;
}

/*!
 * @brief Arguments interpreter. Checks program arguments and sets global variables or exits program if arguments are
 * incorrect.
 * 1. Reads two first arguments - writers and readers count and sets it to global variables.
 * 2. (optional) Checks next arguments and enters debug mode or sets up times.
 *
 * @param argc Arguments count
 * @param argv Array of arguments
 */
void args_interpreter(int argc, char **argv) {
    if (argc < 3) {
        printf(ERROR_ARGUMENTS_MESSAGE);
        exit(EXIT_FAILURE);
    }

    writers_count = atoi(argv[1]);
    readers_count = atoi(argv[2]);

    if (argc > 3 ) {
        if (strcmp(argv[3], "-t") == 0) {
            if (argc < 8) {
                printf(ERROR_ARGUMENTS_MESSAGE);
                exit(EXIT_FAILURE);
            } else {
                int temp;
                min_reading_time = atoi(argv[4]);
                max_reading_time = atoi(argv[5]);
                if (min_reading_time > max_reading_time) {
                    temp = min_reading_time;
                    min_reading_time = max_reading_time;
                    max_reading_time = temp;
                }
                min_writing_time = atoi(argv[6]);
                max_writing_time = atoi(argv[7]);
                if (min_writing_time > max_writing_time) {
                    temp = min_writing_time;
                    min_writing_time = max_writing_time;
                    max_writing_time = temp;
                }
            }
        } else if (strcmp(argv[3], "-debug") == 0) {
            is_debug_run = 1;
        } else {
            printf(ERROR_ARGUMENTS_MESSAGE);
            exit(EXIT_FAILURE);
        }
    }

    if (argc == 9) {
        if (strcmp(argv[8], "-debug") == 0) {
            is_debug_run = 1;
        } else {
            printf(ERROR_ARGUMENTS_MESSAGE);
            exit(EXIT_FAILURE);
        }
    }

    if (argc > 9) {
        printf(ERROR_ARGUMENTS_MESSAGE);
        exit(EXIT_FAILURE);
    }
}

/*!
 * @brief Allocates memory for global variables.
 */
void variables_initializer() {
    srand(time(NULL));
    readers_conds = malloc(readers_count * sizeof(pthread_cond_t));
    writers_conds = malloc(writers_count * sizeof(pthread_cond_t));
    queue = malloc((writers_count + readers_count) * (2 * sizeof(int) + sizeof(time_t)));
    in_library = malloc((writers_count + readers_count) * (2 * sizeof(int) + sizeof(time_t)));
    pthread_mutex_init(&mutex, NULL);
}

/*!
 * @brief Frees memory allocated for global variables.
 */
void cleaner() {
    pthread_mutex_destroy(&mutex);
    int i;
    for (i = 0;i < writers_count;i++) {
        pthread_cond_destroy(&writers_conds[i]);
    }
    for (i = 0;i < readers_count;i++) {
        pthread_cond_destroy(&readers_conds[i]);
    }
    free(in_library);
    free(queue);
    free(writers_conds);
}

#pragma clang diagnostic pop
