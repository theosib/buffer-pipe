#include <poll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Configure buffer size
#define SIZE (1<<30)

// Queue counters
int count=0, head=0, tail=0, size=0;

// Queue buffer
char *buffer = 0;

// How many bytes are stored in the queue?
int queue_bytes()
{
    return count;
}

// How many bytes can be read from the queue in one block?
int dequeue_size()
{
    if (tail == head) { // Full or empty
        if (size == count) { // Full
            // Can dequeue from head to end of buffer
            return size - head;
        } else { // Empty
            return 0;
        }
    } else
    if (tail > head) { // Data is contiguous from head to tail
        return count;
    } else { // Data goes to end of buffer then wraps to the beginning, with hole in the middle
        return size - head;
    }
}

// How many contiguous bytes can we add to the queue?
int enqueue_size()
{
    if (tail == head) { // Full or empty
        if (size == count) { // Full
            return 0;
        } else { // Empty
            // Can enqueue from tail to end of buffer
            return size - tail;
        }
    } else
    if (tail > head) { // Data is contiguous from head to tail
        // Can enqueue from tail to end of buffer
        return size - tail;
    } else { // Data goes to end of buffer then wraps to the beginning, with hole in the middle
        // Can enqueue from tail up to head
        return head - tail;
    }
}

// Pointer whence we can remove bytes
char *remove_ptr()
{
    return buffer + head;
}

// Pointer whither we can add bytes
char *add_ptr()
{
    return buffer + tail;
}

// Deduct n bytes from queue
void remove_bytes(int n)
{
    head = (head + n) & (size - 1);
    count -= n;
}

// Indicate that n bytes have been added to queue
void add_bytes(int n)
{
    tail = (tail + n) & (size - 1);
    count += n;
}

int main(int argc, char *argv[])
{
    int n, m;
    int drain = 0;
    
    // Allocate memory for poll data
    struct pollfd *pfds;
    pfds = calloc(2, sizeof(struct pollfd));
    
    // Allocate memory for data buffer
    if (argc>1) {
        size = atoi(argv[1]);
    } else {
        size = SIZE;
    }
    buffer = malloc(size);
    
    // Set up poll structures
    pfds[STDIN_FILENO].fd = STDIN_FILENO;
    pfds[STDOUT_FILENO].fd = STDOUT_FILENO;
    pfds[STDIN_FILENO].events = POLLIN | POLLHUP | POLLERR;
    pfds[STDOUT_FILENO].events = POLLOUT | POLLHUP | POLLERR;
    
    // Make both stdin and stdout non-blocking
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
    fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL, 0) | O_NONBLOCK);
    
    // Loop until we run empty the queue and the input queue has closed
    while (!drain || queue_bytes() > 0) {
        if (dequeue_size() > 0) {
            // If we have data we want to write out, poll stdout
            pfds[STDOUT_FILENO].fd = STDOUT_FILENO;
        } else {
            // Otherwise don't poll stdout
            pfds[STDOUT_FILENO].fd = -1;
        }
        if (enqueue_size() > 0 && !drain) {
            // If we have free space in queue, poll stdin
            pfds[STDIN_FILENO].fd = STDIN_FILENO;
        } else {
            // Otherwise don't poll stdin
            pfds[STDIN_FILENO].fd = -1;
        }
        
        // Block until we can move data through stdin our stdout
        int ready = poll(pfds, 2, -1);
        if (ready == -1) break;
        
        // If stdout has an error or file closure, just die
        if (pfds[STDOUT_FILENO].revents & (POLLHUP | POLLERR)) {
            perror("STDOUT error");
            break;
        }
        // If stdin has error, die
        if (pfds[STDIN_FILENO].revents & POLLERR) {
            perror("STDIN error");
            break;
        }
        // If stdin has end of file, indicate that we're in draining mode
        if (pfds[STDIN_FILENO].revents & POLLHUP) drain = 1;
        
        // If we can write, try to write some bytes
        if (pfds[STDOUT_FILENO].revents & POLLOUT) {
            n = dequeue_size();
            if (n > 0) {
                m = write(STDOUT_FILENO, remove_ptr(), n);
                if (m < 0) break;
                remove_bytes(m);
            }
        }
        
        // If we can read, try to read some bytes
        if (pfds[STDIN_FILENO].revents & POLLIN) {
            n = enqueue_size();
            if (n > 0) {
                m = read(STDIN_FILENO, add_ptr(), n);
                if (m < 0) break;
                add_bytes(m);
            }
        }
    }
    
    // Pedantic unnecessary freeing of memory
    free(pfds);
    free(buffer);
    
    return 0;
}
