#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>

// Constants
#define NUM_ENGINEERS 3
#define NUM_SATELLITES 5
#define TIMEOUT_SECONDS 5

// Satellite priority levels
typedef enum {
    COMMERCIAL = 1,
    WEATHER = 2,
    MILITARY = 3
} Priority;

// Satellite structure
typedef struct {
    int id;
    Priority priority;
    pthread_t thread;
    int is_handled;
    int is_timed_out;
} Satellite;

// Engineer structure
typedef struct {
    int id;
    pthread_t thread;
    int total_serviced;
    int should_terminate;
} Engineer;

// Shared resources
int availableEngineers = NUM_ENGINEERS;
pthread_mutex_t engineerMutex = PTHREAD_MUTEX_INITIALIZER;
sem_t newRequest;
sem_t requestHandled;
int all_satellites_processed = 0;

// Priority queue structure
typedef struct {
    Satellite* satellites[NUM_SATELLITES];
    int size;
} PriorityQueue;

PriorityQueue requestQueue = {.size = 0};

// Function prototypes
void* satellite_thread(void* arg);
void* engineer_thread(void* arg);
void add_to_queue(Satellite* satellite);
Satellite* get_highest_priority_satellite();
void remove_from_queue(Satellite* satellite);
const char* priority_to_string(Priority p);

int main() {
    printf("=== Satellite Ground Station Simulation ===\n");
    printf("Number of Engineers: %d\n", NUM_ENGINEERS);
    printf("Number of Satellites: %d\n", NUM_SATELLITES);
    printf("Timeout: %d seconds\n\n", TIMEOUT_SECONDS);

    // Initialize semaphores
    sem_init(&newRequest, 0, 0);
    sem_init(&requestHandled, 0, 0);

    // Create engineer threads
    Engineer engineers[NUM_ENGINEERS];
    for (int i = 0; i < NUM_ENGINEERS; i++) {
        engineers[i].id = i + 1;
        engineers[i].total_serviced = 0;
        engineers[i].should_terminate = 0;
        pthread_create(&engineers[i].thread, NULL, engineer_thread, &engineers[i]);
    }

    // Create satellite threads
    Satellite satellites[NUM_SATELLITES];
    srand(time(NULL));
    for (int i = 0; i < NUM_SATELLITES; i++) {
        satellites[i].id = i + 1;
        satellites[i].priority = (rand() % 3) + 1;
        satellites[i].is_handled = 0;
        satellites[i].is_timed_out = 0;
        pthread_create(&satellites[i].thread, NULL, satellite_thread, &satellites[i]);
    }

    // Wait for all satellite threads to complete
    for (int i = 0; i < NUM_SATELLITES; i++) {
        pthread_join(satellites[i].thread, NULL);
    }

    // Signal engineers to terminate
    all_satellites_processed = 1;
    for (int i = 0; i < NUM_ENGINEERS; i++) {
        engineers[i].should_terminate = 1;
        sem_post(&newRequest); // Wake up engineers to check termination
    }

    // Wait for all engineer threads to complete
    for (int i = 0; i < NUM_ENGINEERS; i++) {
        pthread_join(engineers[i].thread, NULL);
    }

    // Print final statistics
    printf("\n=== Final Statistics ===\n");
    for (int i = 0; i < NUM_SATELLITES; i++) {
        printf("Satellite %d (Priority: %s): %s\n",
               satellites[i].id,
               priority_to_string(satellites[i].priority),
               satellites[i].is_handled ? "Serviced" : "Timed Out");
    }

    printf("\nEngineer Statistics:\n");
    for (int i = 0; i < NUM_ENGINEERS; i++) {
        printf("Engineer %d serviced %d satellites\n", 
               engineers[i].id, engineers[i].total_serviced);
    }

    // Cleanup
    sem_destroy(&newRequest);
    sem_destroy(&requestHandled);
    pthread_mutex_destroy(&engineerMutex);

    return 0;
}

void* satellite_thread(void* arg) {
    Satellite* satellite = (Satellite*)arg;
    printf("Satellite %d (Priority: %s) requesting support\n", 
           satellite->id, priority_to_string(satellite->priority));

    // Add to request queue
    pthread_mutex_lock(&engineerMutex);
    add_to_queue(satellite);
    pthread_mutex_unlock(&engineerMutex);

    // Signal new request
    sem_post(&newRequest);

    // Wait for request to be handled with timeout
    time_t start_time = time(NULL);
    while (!satellite->is_handled && !satellite->is_timed_out) {
        if (sem_trywait(&requestHandled) == 0) {
            satellite->is_handled = 1;
            printf("Satellite %d received support\n", satellite->id);
            break;
        }
        
        if (time(NULL) - start_time >= TIMEOUT_SECONDS) {
            satellite->is_timed_out = 1;
            printf("Satellite %d timed out\n", satellite->id);
            // Remove from queue if timeout
            pthread_mutex_lock(&engineerMutex);
            remove_from_queue(satellite);
            pthread_mutex_unlock(&engineerMutex);
            break;
        }
        
        usleep(100000); // Sleep for 100ms
    }

    return NULL;
}

void* engineer_thread(void* arg) {
    Engineer* engineer = (Engineer*)arg;
    printf("Engineer %d started\n", engineer->id);

    while (!engineer->should_terminate) {
        // Wait for new request
        sem_wait(&newRequest);

        // Check if we should terminate
        if (engineer->should_terminate) {
            break;
        }

        // Lock mutex and get satellite
        pthread_mutex_lock(&engineerMutex);
        availableEngineers--;
        Satellite* satellite = get_highest_priority_satellite();
        pthread_mutex_unlock(&engineerMutex);

        if (satellite != NULL) {
            printf("Engineer %d servicing Satellite %d (Priority: %s)\n", 
                   engineer->id, satellite->id, 
                   priority_to_string(satellite->priority));
            
            // Simulate service time
            sleep(1);

            // Mark satellite as handled
            satellite->is_handled = 1;
            engineer->total_serviced++;
            
            // Signal request handled
            sem_post(&requestHandled);

            // Update available engineers
            pthread_mutex_lock(&engineerMutex);
            availableEngineers++;
            pthread_mutex_unlock(&engineerMutex);
        }
    }

    printf("Engineer %d terminated after servicing %d satellites\n", 
           engineer->id, engineer->total_serviced);
    return NULL;
}

void add_to_queue(Satellite* satellite) {
    if (requestQueue.size < NUM_SATELLITES) {
        requestQueue.satellites[requestQueue.size++] = satellite;
    }
}

void remove_from_queue(Satellite* satellite) {
    for (int i = 0; i < requestQueue.size; i++) {
        if (requestQueue.satellites[i] == satellite) {
            // Shift remaining elements
            for (int j = i; j < requestQueue.size - 1; j++) {
                requestQueue.satellites[j] = requestQueue.satellites[j + 1];
            }
            requestQueue.size--;
            break;
        }
    }
}

Satellite* get_highest_priority_satellite() {
    if (requestQueue.size == 0) return NULL;

    Satellite* highest = requestQueue.satellites[0];
    int highest_idx = 0;

    for (int i = 1; i < requestQueue.size; i++) {
        if (requestQueue.satellites[i]->priority > highest->priority) {
            highest = requestQueue.satellites[i];
            highest_idx = i;
        }
    }

    // Remove the satellite from queue
    for (int i = highest_idx; i < requestQueue.size - 1; i++) {
        requestQueue.satellites[i] = requestQueue.satellites[i + 1];
    }
    requestQueue.size--;

    return highest;
}

const char* priority_to_string(Priority p) {
    switch (p) {
        case COMMERCIAL: return "Commercial";
        case WEATHER: return "Weather";
        case MILITARY: return "Military";
        default: return "Unknown";
    }
} 