#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)


void sleep_ms(int milliseconds) {
    usleep(milliseconds * 1000);  // Convert milliseconds to microseconds
}

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    
    sleep_ms(thread_func_args->wait_to_obtain_ms);

    // Obtain mutex
    pthread_mutex_lock(thread_func_args->mutex);

    // Critical section
    printf("Thread has obtained the mutex\n");

    // Wait while holding the mutex
    sleep_ms(thread_func_args->wait_to_release_ms);

    // Release mutex
    pthread_mutex_unlock(thread_func_args->mutex);
    printf("Thread has released the mutex\n");
	
    thread_func_args->thread_complete_success=true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
     
     struct thread_data* thread_obj=(struct thread_data *)malloc(sizeof(struct thread_data));
     
     
     thread_obj->mutex=mutex;
     
     //pthread_mutex_init(thread_obj->mutex,NULL);
     
     thread_obj->thread_complete_success=false;
     thread_obj->wait_to_obtain_ms=wait_to_obtain_ms;
     thread_obj->wait_to_release_ms=wait_to_release_ms;
     
     int rc=pthread_create(thread, NULL, threadfunc, (void *) thread_obj);
     
     if(!rc)     
     return true;
     
     return rc;
}

