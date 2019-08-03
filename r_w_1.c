/*!
 * @file
 * Readers and Writers - implementation 1
 *
 * Implementation with no starvation of writers or readers. It uses conditional variables - one for readers and one for
 * each writer. In this implementation there is an additional thread - librarian. It waits for some (default random) time
 * allowing readers to use library. When time ends - it stops letting readers to enter library. When library is empty -
 * librarian looks for writer with maximum waiting time and lets him in.
 *
 * @author Mateusz Wawreszuk
 */

/*! \mainpage ReadersAndWriters - opis.
\htmlinclude main_page.html
*/

#pragma clang diagnostic push
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
#define ERROR_ARGUMENTS_MESSAGE "Usage: ReaderAndWriters1 number_of_writers number_of_readers [-t min_reading_time max_reading_time min_writing_time max_writing_time min_allow_read_time max_allow_read_time] [-debug]\n"

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
 * @brief Conditional variable to handle readers.
 */
pthread_cond_t readers_cond;
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
 * @brief Array of timestamps to set when writer enters library. Set to 0 if writer is not in library.
 *
 * Position in array is an identifier of writer.
 */
time_t *writers_in_library;
/*!
 * @brief Number of writers currently in library.
 */
int writers_in_library_count = 0;

/*!
 * @brief Array of timestamps to set when reader enters library. Set to 0 if reader is not in library.
 *
 * Position in array is an identifier of reader.
 */
time_t *readers_in_library;
/*!
 * @brief Number of readers currently in library.
 */
int readers_in_library_count = 0;

/*!
 * @brief Array of timestamps to set when writer starts waiting to enter library. Set to 0 if writer is not in queue.
 */
time_t *writers_queue;
/*!
 * @brief Number of writers in library.
 */
int writers_queue_count;

/*!
 * @brief Array of timestamps to set when reader starts waiting to enter library. Set to 0 if reader is not in queue.
 */
time_t *readers_queue;
/*!
 * @brief Number of readers in library.
 */
int readers_queue_count;

/*!
 * @brief Notifies that some writer is in library or is going to be let to the library.
 */
int writer_notification = 0;

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
 * @brief Minimum time that librarian lets readers to read before he stops letting them in.
 */
int min_allow_read_time = 10;
/*!
 * @brief Maximum time that librarian lets readers to read before he stops letting them in.
 */
int max_allow_read_time = 20;

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
 * @brief Prints library and queues state. There are two different of printing - one standard and one for debug mode.
 *
 * In standard mode function prints message in format:
 * ReaderQ: readers_in_queue WriterQ: writers_in_queue [ in: R:readers_in_library W:writers_in_library ]
 *
 * In debug mode function prints all threads with theirs numbers - grouped to readers queue, writers queue and library.
 * Format:
 * Readers queue (seconds in queue):
 * Reader reader_number (seconds_in_queue)
 * (...)
 *
 * Writers queue (seconds in queue):
 * Writer writer_number (seconds_in_queue)
 * (...)
 *
 * In library (seconds in library):
 * Writer writer_number (seconds_in_queue) or Reader reader_number (seconds_in_queue)
 * (...)
 */
void print() {
    if (is_debug_run == 0) {
        printf("ReaderQ: %i\t", readers_queue_count);
        printf("WriterQ: %i\t", writers_queue_count);
        printf("[ in: R:%i\t", readers_in_library_count);
        printf("W:%i ]\n", writers_in_library_count);
    } else {
        int i;
        for (i = 0;i < 100;i++) {
            printf("\n");
        }
        printf("Readers queue (seconds in queue):\n");
        for (i = 0;i < readers_count;i++) {
            if (readers_queue[i] != 0) {
                printf("Reader %i\t", i);
                printf("(%li)\n", get_timestamp() - readers_queue[i]);
            }
        }
        printf("\nWriters queue (seconds in queue):\n");
        for (i = 0;i < writers_count;i++) {
            if (writers_queue[i] != 0) {
                printf("Writer %i\t", i);
                printf("(%li)\n", get_timestamp() - writers_queue[i]);
            }
        }
        printf("\nIn library (seconds in library):\n");
        for (i = 0;i < writers_count;i++) {
            if (writers_in_library[i] != 0) {
                printf("Writer %i\t", i);
                printf("(%li)\n", get_timestamp() - writers_in_library[i]);
            }
        }
        for (i = 0;i < readers_count;i++) {
            if (readers_in_library[i] != 0) {
                printf("Reader %i\t", i);
                printf("(%li)\n", get_timestamp() - readers_in_library[i]);
            }
        }
    }
}

/*!
 * @brief Reader threat. It works in endless loop. Function checks is writer_notification flag set - if it's not
 * Reader enters library. If writer_notification is set, thread is waiting for signal from conditional variable
 * readers_cond.
 *
 * @param arg Reader id
 */
void* reader(void* arg) {
    int reader_id = *((int *) arg);
    while (1) {
        pthread_mutex_lock(&mutex);
        if (writer_notification) {
            pthread_cond_wait(&readers_cond, &mutex);
        }
        pthread_mutex_unlock(&mutex);
        read_books(reader_id);
    }
}

/*!
 * @brief Writer thread. It works in endless loop. Function waits for signal from conditional variable assigned to this
 * thread (array *writers_cond), then it waits till readers leave library and finally it enters library to write a book.
 * When book is ready (writer left library) function sends signal to readers conditional variable (readers_cond).
 *
 * @param arg Writer id
 */
void* writer(void* arg) {
    int writer_id = *((int *) arg);
    while (1) {
        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&writers_conds[writer_id], &mutex);
        pthread_mutex_unlock(&mutex);
        while (readers_in_library_count);
        write_book(writer_id);
        pthread_cond_broadcast(&readers_cond);
    }
}

/*!
 * @brief Librarian thread. It works in endless loop. Function sleeps for some random time (default 10-20 seconds, it
 * can be changed by main function arguments) and then checks are writers in library. If there is no writers, function
 * sets writer_notification flag, then it looks for writer that waits for longest time and sends signal to conditional
 * variable assigned to this writer id. Then it waits till writers leave library and starts all over again.
 */
void* librarian() {
    while (1) {
        sleep(get_random(min_allow_read_time, max_allow_read_time));
        if (!writers_in_library_count) {
            writer_notification = 1;
            int i, time, max_waiter_id = 0, max_time = 0;
            for (i = 0;i < writers_count;i++) {
                time = get_timestamp() - writers_queue[i];
                if (time > max_time) {
                    max_time = time;
                    max_waiter_id = i;
                }
            }
            pthread_cond_signal(&writers_conds[max_waiter_id]);
        }
        while (writers_in_library_count);
    }
}

/*!
 * @brief Function symbolises entering library by a writer and writing a book. It sets enter library timestamp in array
 * *writers_in_library (in writer_id position). Then function resets corresponding timestamp in array *writers_queue,
 * increases writers_in_library_count and decreases writers_queue_count. After that function sleeps for some random time
 * (by default 5-15 seconds, it can be changed by main function arguments) and leaves library (sets timestamp in
 * *writers_queue, resets timestamp in *writers_in_library array, increases writers_queue_count and decreases
 * writers_in_library_count).
 *
 * @param writer_id Writer thread id
 */
void write_book(int writer_id) {
    pthread_mutex_lock( &mutex );

    writers_in_library[writer_id] = get_timestamp();
    writers_queue[writer_id] = 0;
    writers_in_library_count++;
    writers_queue_count--;

    print();

    pthread_mutex_unlock( &mutex );

    sleep(get_random(min_writing_time, max_writing_time));

    pthread_mutex_lock( &mutex );

    writers_in_library[writer_id] = 0;
    writers_queue[writer_id] = get_timestamp();
    writers_in_library_count--;
    writers_queue_count++;
    writer_notification = 0;

    print();

    pthread_mutex_unlock( &mutex );
}

/*!
 * @brief Function symbolises entering library by a reader and reading books. It sets enter library timestamp in array
 * *readers_in_library (in reader_id position). Then function resets corresponding timestamp in array *readers_queue,
 * increases readers_in_library_count and decreases readers_queue_count. After that function sleeps for some random time
 * (by default 0-5 seconds, it can be changed by main function arguments) and leaves library (sets timestamp in
 * *readers_queue, resets timestamp in *readers_in_library array, increases readers_queue_count and decreases
 * readers_in_library_count).
 *
 * @param reader_id Reader thread id
 */
void read_books(int reader_id) {
    pthread_mutex_lock( &mutex );

    readers_in_library[reader_id] = get_timestamp();
    readers_queue[reader_id] = 0;
    readers_in_library_count++;
    readers_queue_count--;

    print();

    pthread_mutex_unlock( &mutex );

    sleep(get_random(min_reading_time, max_reading_time));

    pthread_mutex_lock( &mutex );

    readers_in_library[reader_id] = 0;
    readers_queue[reader_id] = get_timestamp();
    readers_in_library_count--;
    readers_queue_count++;

    print();

    pthread_mutex_unlock( &mutex );
}

/*!
 * @brief Function initialises writers_queue and readers_queue with actual timestamp.
 */
void init_queue() {
    int i;
    time_t timestamp = get_timestamp();
    for (i = 0;i < readers_count;i++) {
        readers_queue[i] = timestamp;
    }
    for (i = 0;i < writers_count;i++) {
        writers_queue[i] = timestamp;
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
    writers_queue_count = writers_count;
    readers_count = atoi(argv[2]);
    readers_queue_count = readers_count;

    if (argc > 3 ) {
        if (strcmp(argv[3], "-t") == 0) {
            if (argc < 10) {
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
                min_allow_read_time = atoi(argv[8]);
                max_allow_read_time = atoi(argv[9]);
                if (min_allow_read_time > max_allow_read_time) {
                    temp = min_allow_read_time;
                    min_allow_read_time = max_allow_read_time;
                    max_allow_read_time = temp;
                }
            }
        } else if (strcmp(argv[3], "-debug") == 0) {
            is_debug_run = 1;
        } else {
            printf(ERROR_ARGUMENTS_MESSAGE);
            exit(EXIT_FAILURE);
        }
    }

    if (argc == 11) {
        if (strcmp(argv[10], "-debug") == 0) {
            is_debug_run = 1;
        } else {
            printf(ERROR_ARGUMENTS_MESSAGE);
            exit(EXIT_FAILURE);
        }
    }

    if (argc > 11) {
        printf(ERROR_ARGUMENTS_MESSAGE);
        exit(EXIT_FAILURE);
    }
}

/*!
 * @brief Allocates memory for global variables.
 */
void variables_initializer() {
    srand(time(NULL));
    writers_in_library = malloc(writers_count * sizeof(time_t));
    writers_queue = malloc(writers_count * sizeof(time_t));
    readers_in_library = malloc(readers_count * sizeof(time_t));
    readers_queue = malloc(readers_count * sizeof(time_t));
    writers_conds = malloc(writers_count * sizeof(pthread_cond_t));
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&readers_cond, NULL);
}

/*!
 * @brief Frees memory allocated for global variables.
 */
void cleaner() {
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&readers_cond);
    for (int i = 0;i < writers_count;i++) {
        pthread_cond_destroy(&writers_conds[i]);
    }
    free(writers_in_library);
    free(writers_queue);
    free(readers_in_library);
    free(readers_queue);
    free(writers_conds);
}

#pragma clang diagnostic pop
