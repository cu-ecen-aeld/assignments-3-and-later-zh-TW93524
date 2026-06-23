#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
//#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // Cast the parameter to the correct structure type
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // 1. Wait before trying to obtain the mutex
    DEBUG_LOG( "Start Wait usleep" );
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // 2. Obtain the mutex
    DEBUG_LOG( "Try obtain mutex" );
    if (pthread_mutex_lock(thread_func_args->mutex) != 0) {
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // 3. Wait while holding the mutex
    DEBUG_LOG( "Wait when holding mutex" );
    usleep(thread_func_args->wait_to_release_ms * 1000);

    // 4. Release the mutex
    DEBUG_LOG( "Release the mutex" );
    if (pthread_mutex_unlock(thread_func_args->mutex) != 0) {
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    thread_func_args->thread_complete_success = true;
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    // 1. Allocate memory for thread_data on the heap so it persists when this function returns
    struct thread_data* data = (struct thread_data*) malloc(sizeof(struct thread_data));
    if (data == NULL) {
        return false; // Memory allocation failed
    }

    // 2. Setup mutex and wait arguments
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;

    // 3. Create the thread passing data to threadfunc
    DEBUG_LOG( "Create thread" );
    int rc = pthread_create(thread, NULL, threadfunc, (void*)data);
    
    if (rc != 0) {
        // Thread creation failed, clean up allocated memory
        free(data);
        return false;
    }

    // Thread created successfully
    return true;
}

