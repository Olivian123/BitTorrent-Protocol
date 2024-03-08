#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define PATH_LENGTH 32
#define MAX_CHUNKS 100

#define INIT_CHANNEL 1
#define TRACKER_DTH_CHANNEL 0
#define DTH_UTH_CHANNEL 2
#define UPDATE_CHANNEL 6
#define FINISH_CHANNEL 4
#define ACK_CHANNEL 5

typedef struct
{
    char *file_name;
    int nr_of_chunks;
    char **chunks;
} FileStruct;

typedef struct
{
    int nr_of_files;
    FileStruct **files;
} PeersFilesList;

int max(int num1, int num2)
{
    return (num1 > num2 ) ? num1 : num2;
}

// Finds the proccesses that have the requested file and
// returns an array with their ranks
int *what_proc_has_my_file(PeersFilesList **files_list, int numtasks, int *ret_arr_size, char *file_name)
{
    int *arr = (int *)malloc(sizeof(int));
    if (!arr)
        exit(1);

    *ret_arr_size = 0;

    for (int i = 1; i < numtasks; i++)
    {
        if (!files_list[i])
            continue;

        FileStruct **files = files_list[i]->files;

        for (int j = 0; j < files_list[i]->nr_of_files; j++)
        {
            if (strcmp(files[j]->file_name, file_name))
                continue;

            (*ret_arr_size)++;
            arr = realloc(arr, *ret_arr_size * sizeof(int));
            if (!arr)
                exit(1);

            arr[*ret_arr_size - 1] = i;
        }
    }

    return arr;
}

// Returns the file position in a vecotr of files
// If the file is not in the vector returns -1
int find_file_pos(FileStruct **files, int nr_of_files, char *file_name)
{
    for (int i = 0; i < nr_of_files; i++)
        if (!strcmp(files[i]->file_name, file_name))
            return i;

    return -1;
}

FileStruct *alloc_file_structure()
{
    FileStruct *new_file_structure = (FileStruct *)malloc(sizeof(FileStruct));
    if (!new_file_structure)
        exit(1);

    new_file_structure->file_name = (char *)malloc(sizeof(char) * (MAX_FILENAME + 1));
    if (!new_file_structure->file_name)
        exit(1);

    new_file_structure->chunks = (char **)malloc(sizeof(char *) * MAX_CHUNKS);
    if (!new_file_structure->chunks)
        exit(1);

    for (int i = 0; i < MAX_CHUNKS; i++)
    {
        new_file_structure->chunks[i] = (char *)malloc(sizeof(char) * (HASH_SIZE + 1));
        if (!new_file_structure->chunks[i])
            exit(1);
    }

    return new_file_structure;
}

void deallocate_file_struct_array(FileStruct ***file_array, int num_files)
{
    if (!file_array || !(*file_array))
        return;  // Nothing to deallocate

    for (int i = 0; i < num_files; ++i)
    {
        FileStruct *file = (*file_array)[i];

        if (file)
        {
            free(file->file_name);

            for (int j = 0; j < file->nr_of_chunks; ++j)
            {
                free(file->chunks[j]);
            }

            free(file->chunks);
            free(file);
        }
    }

    free(*file_array);
    *file_array = NULL;  // Set the pointer to NULL to avoid dangling pointers
}

void deallocate_peers_files_list(PeersFilesList ***peers_files_list, int numtasks)
{
    if (!peers_files_list || !(*peers_files_list))
        return;  // Nothing to deallocate

    for (int i = 1; i < numtasks; i++)
    {
        deallocate_file_struct_array(&((*peers_files_list)[i]->files), (*peers_files_list)[i]->nr_of_files);
        free((*peers_files_list)[i]);
    }

    free(*peers_files_list);
    *peers_files_list = NULL;
}

void send_file_struct(FileStruct *file_info, int dest, int tag)
{
    MPI_Send(file_info->file_name, MAX_FILENAME, MPI_CHAR, dest, tag, MPI_COMM_WORLD);
    MPI_Send(&(file_info->nr_of_chunks), 1, MPI_INT, dest, tag, MPI_COMM_WORLD);

    for (int i = 0; i < file_info->nr_of_chunks; i++)
        MPI_Send(file_info->chunks[i], HASH_SIZE + 1, MPI_CHAR, dest, tag, MPI_COMM_WORLD);
}

void send_file_struct_arr(FileStruct **file_info_arr, int *nr_of_files, int dest, int tag)
{
    // Send to destination the number of files that the peer has
    MPI_Send(nr_of_files, 1, MPI_INT, dest, tag, MPI_COMM_WORLD);

    // Send to destination what files the peer has
    for (int i = 0; i < *nr_of_files; i++)
        send_file_struct(file_info_arr[i], dest, tag);
}

void send_file_list(PeersFilesList **file_list, int *nr_peers, int dest, int tag)
{
    // Send the number of peers
    MPI_Send(nr_peers, 1, MPI_INT, dest, 0, MPI_COMM_WORLD);

    // Send the FileInfo arr
    for (int i = 0; i < *nr_peers; i++)
        send_file_struct_arr(file_list[i]->files, &(file_list[i]->nr_of_files), dest, tag);
}

void recieve_file_struct(FileStruct *recieved_file, int source, int tag)
{
    MPI_Recv(recieved_file->file_name, MAX_FILENAME, MPI_CHAR, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&(recieved_file->nr_of_chunks), 1, MPI_INT, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    for (int i = 0; i < recieved_file->nr_of_chunks; i++)
        MPI_Recv(recieved_file->chunks[i], HASH_SIZE + 1, MPI_CHAR, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

// Receives a file struct, If needed alloc/realloc memory to store the data
void recieve_file_struct_arr(FileStruct ***recieved_files, int *nr_of_files, int source, int tag)
{
    int old_nr_of_files = *nr_of_files;

    MPI_Recv(nr_of_files, 1, MPI_INT, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // If the address is null alloc memorz
    if (!(*recieved_files))
    {
        *recieved_files = (FileStruct **)malloc(*nr_of_files * sizeof(FileStruct *));
        if (!(*recieved_files))
        {
            exit(3);
        }
    }

    if(old_nr_of_files != *nr_of_files) {
        *recieved_files = (FileStruct **)realloc(*recieved_files, *nr_of_files * sizeof(FileStruct *));
        if (!(*recieved_files))
        {
            exit(3);
        }
    }

    for (int i = 0; i < *nr_of_files; i++)
    {
        if (i >= old_nr_of_files) 
            (*recieved_files)[i] = alloc_file_structure();    

        recieve_file_struct((*recieved_files)[i], source, tag);
    
    }
}

// Allocs memory for PeersFliesList
// If memory allocation is needed set firsts argument adress as null
void recieve_filles_list(PeersFilesList ***peers_files_list, int numtasks, int tag)
{
    int ok_alloc = !(*peers_files_list);

    if (ok_alloc)
    {
        *peers_files_list = (PeersFilesList **)malloc(numtasks * sizeof(PeersFilesList *));
        if (!*peers_files_list)
            exit(1);

        // Initialize the entire array
        for (int i = 1; i < numtasks; i++)
        {
            (*peers_files_list)[i] = NULL;
        }
    }

    for (int i = 1; i < numtasks; i++)
    {
        // Allocate if not already allocated
        if (!(*peers_files_list)[i])
        {
            (*peers_files_list)[i] = (PeersFilesList *)malloc(sizeof(PeersFilesList));
            if (!(*peers_files_list)[i])
                exit(2);

            (*peers_files_list)[i]->nr_of_files = 0;
            (*peers_files_list)[i]->files = NULL;
        }

        recieve_file_struct_arr(&((*peers_files_list)[i]->files), &((*peers_files_list)[i]->nr_of_files), i, tag);
    }
}

// Gets the file data from the file and stores it in an array of FileStructs
FileStruct **get_file_data(int *nr_of_files, int *nr_of_wanted_files, int rank)
{
    char filename[PATH_LENGTH];

    sprintf(filename, "in%d.txt", rank);
    FILE *file = fopen(filename, "r");

    fscanf(file, "%d\n", nr_of_files);

    FileStruct **files = (FileStruct **)malloc(*nr_of_files * sizeof(FileStruct *));
    if (!files)
    {
        exit(1);
    }

    for (int i = 0; i < *nr_of_files; i++)
    {
        files[i] = alloc_file_structure();

        fscanf(file, "%s %d\n", files[i]->file_name, &files[i]->nr_of_chunks);

        for (int j = 0; j < files[i]->nr_of_chunks; j++)
            fscanf(file, "%s\n", files[i]->chunks[j]);
    }

    fscanf(file, "%d\n", nr_of_wanted_files);

    files = (FileStruct **)realloc(files, (*nr_of_files + *nr_of_wanted_files) * sizeof(FileStruct *));
    if (!files)
    {
        exit(1);
    }

    for (int i = 0; i < *nr_of_wanted_files; i++)
    {
        files[i + *nr_of_files] = alloc_file_structure();
        files[i + *nr_of_files]->nr_of_chunks = 0;

        fscanf(file, "%s\n", files[i + *nr_of_files]->file_name);
    }

    return files;
}

void saveHashListToFile(int rank, FileStruct * file) {
    // Constructing the name of the file
    char new_file_name[50];
    snprintf(new_file_name, sizeof(new_file_name), "client%d_%s", rank, file -> file_name);

    FILE* new_file = fopen(new_file_name, "w");

    if (new_file == NULL)
        exit(EXIT_FAILURE);

    // Saving the chunks
    for (int i = 0; i < file -> nr_of_chunks; ++i) {
        fprintf(new_file, "%s\n", file -> chunks[i]);
    }

    fclose(new_file);
}

void tracker(int numtasks, int rank)
{
    int nr = 0;

    PeersFilesList **peers_files_list = NULL;
    recieve_filles_list(&peers_files_list, numtasks, INIT_CHANNEL);

    // Send "START" signal to clients/peers
    for(int i = 1; i < numtasks; i++)
        MPI_Send("START", 6, MPI_CHAR, i, INIT_CHANNEL, MPI_COMM_WORLD);

    // Until each client did not recived it's wanted files
    // the tracker works
    while (1)
    {
        for (int i = 1; i < numtasks; i++)
        {
            int flag;

            // Probing for updating peers files list
            MPI_Iprobe(i, UPDATE_CHANNEL, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);

            if (flag)
            {
                recieve_file_struct_arr(&(peers_files_list[i]->files), &(peers_files_list[i]->nr_of_files), i, UPDATE_CHANNEL);                
            }

            // Probing for requesting what cuncks of the file that the porc wants
            // each other porc has
            MPI_Iprobe(i, TRACKER_DTH_CHANNEL, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);

            if (!flag)
            {
                continue;
            }

            char file_name[MAX_FILENAME + 1];
            MPI_Recv(file_name, MAX_FILENAME + 1, MPI_CHAR, i, TRACKER_DTH_CHANNEL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (!strncmp(file_name, "STOP", 4))
            {
                nr++;
                continue;
            }

            int arr_size;
            int *arr = what_proc_has_my_file(peers_files_list, numtasks, &arr_size, file_name);

            MPI_Send(&arr_size, 1, MPI_INT, i, TRACKER_DTH_CHANNEL, MPI_COMM_WORLD);
            MPI_Send(arr, arr_size, MPI_INT, i, TRACKER_DTH_CHANNEL, MPI_COMM_WORLD);

            if (arr_size == 0)
                continue;

            for (int j = 0; j < arr_size; j++)
            {
                int pos = find_file_pos(peers_files_list[arr[j]]->files, peers_files_list[arr[j]]->nr_of_files, file_name);
                send_file_struct(peers_files_list[arr[j]]->files[pos], i, TRACKER_DTH_CHANNEL);
            }
        }

        // All clients got the files that they wanted so the tracker
        // stops porviding informations
        if (nr == numtasks - 1)
            break;
    }

    // Send "FINSH" to upload threads
    for (int i = 1; i < numtasks; i++)
        MPI_Send("FINISH", 8, MPI_CHAR, i, FINISH_CHANNEL, MPI_COMM_WORLD);

    deallocate_peers_files_list(&peers_files_list, numtasks);
}

void *download_thread_func(void *arg)
{
    PeersFilesList info = *(PeersFilesList *)arg;
    
    int rank = info.nr_of_files / 1000;
    int nr_of_files = (info.nr_of_files % 1000) / 100;
    int nr_of_wanted_files = info.nr_of_files % 100;

    while (nr_of_wanted_files > 0)
    {
        char *file_name = info.files[nr_of_files]->file_name;
        int nr_of_chunks = info.files[nr_of_files]->nr_of_chunks;

        int max_nr_chunks = 0;

        // Send request for the file that the proccess needs
        MPI_Send(file_name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TRACKER_DTH_CHANNEL, MPI_COMM_WORLD);

        int nr_seeds;
        int *arr = (int *)malloc(sizeof(int) * nr_seeds);
        if (!arr)
            exit(1);

        // Receiving a vector of the peers that got the requested
        MPI_Recv(&nr_seeds, 1, MPI_INT, TRACKER_RANK, TRACKER_DTH_CHANNEL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(arr, nr_seeds, MPI_INT, TRACKER_RANK, TRACKER_DTH_CHANNEL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        FileStruct **files = (FileStruct **)malloc(sizeof(FileStruct **) * nr_seeds);
        if (!files)
            exit(1);

        for(int i = 0; i < nr_seeds; i++)
            files[i] = NULL;

        // Receiving a list of peers that have the file and
        // the segments that they got
        for (int i = 0; i < nr_seeds; i++)
        {
            files[i] = alloc_file_structure();
            recieve_file_struct(files[i], TRACKER_RANK, TRACKER_DTH_CHANNEL);

            max_nr_chunks = max(files[i] -> nr_of_chunks, max_nr_chunks);
        }

        // Choosing a peer 
        int seed = 0;
        while (1)
        {
            seed = rand() % nr_seeds;

            if (files[seed]->nr_of_chunks >= nr_of_chunks + 10 ||
                files[seed]->nr_of_chunks == max_nr_chunks)
                break;
        }

        // Asking for 10 chunks until updating the tracker
        for (int i = 0; i < 10; i++)
        {
            // Send request to seed for the next chunk that I need
            MPI_Send(file_name, MAX_FILENAME, MPI_CHAR, arr[seed], DTH_UTH_CHANNEL, MPI_COMM_WORLD);
            MPI_Send(files[seed] -> chunks[nr_of_chunks], HASH_SIZE, MPI_CHAR, arr[seed], DTH_UTH_CHANNEL, MPI_COMM_WORLD);

            // Wait for reciveing the ack
            char ack[4];
            MPI_Recv(ack, ACK_CHANNEL, MPI_CHAR, arr[seed], 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            info.files[nr_of_files]->chunks[nr_of_chunks] = files[seed] -> chunks[nr_of_chunks];
            info.files[nr_of_files]->nr_of_chunks ++;
            nr_of_chunks ++;

            // If the file is completed the 10 steps are stoped
            // The tracker needs to be updated anyway
            if (nr_of_chunks == max_nr_chunks) {
                saveHashListToFile(rank, info.files[nr_of_files]);
                
                nr_of_files ++;
                nr_of_wanted_files --;
               
                break;
            }
        }

        // Update the tracker about the files chunks that the client recived
        send_file_struct_arr(info.files, &nr_of_files, TRACKER_RANK, UPDATE_CHANNEL);
    }

    MPI_Send("STOP", MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TRACKER_DTH_CHANNEL, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg)
{
    int numtasks = *(int *) arg;

    // Work until "STOP" signal is sent
    while (1)
    {
        int flag;
        MPI_Iprobe(TRACKER_RANK, FINISH_CHANNEL, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);

        if (flag)
        {
            break;
        }

        for (int i = 1; i < numtasks; i++)
        {
            // Cheking is a signal was sent 
            int flag;
            MPI_Iprobe(i, DTH_UTH_CHANNEL, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);

            if (!flag)
            {
                continue;
            }

            char file_name[MAX_FILENAME];
            char chunk[HASH_SIZE];

            MPI_Recv(file_name, MAX_FILENAME, MPI_CHAR, i, DTH_UTH_CHANNEL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(chunk, HASH_SIZE, MPI_CHAR, i, DTH_UTH_CHANNEL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            MPI_Send("ACK", ACK_CHANNEL, MPI_CHAR, i, 5, MPI_COMM_WORLD);
        }
    }

    return NULL;
}

void peer(int numtasks, int rank)
{
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    int nr_of_files = 0;
    int nr_of_wanted_files = 0;
    FileStruct **files = get_file_data(&nr_of_files, &nr_of_wanted_files, rank);
    send_file_struct_arr(files, &nr_of_files, TRACKER_RANK, INIT_CHANNEL);

    // Wait for "START" signal
    char start[7];
    MPI_Recv(start, 6, MPI_CHAR, TRACKER_RANK, INIT_CHANNEL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    
    PeersFilesList temp;
    temp.files = files;
    temp.nr_of_files = 1000 * rank + 100 * nr_of_files + nr_of_wanted_files;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&temp);
    if (r)
    {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&numtasks);
    if (r)
    {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r)
    {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r)
    {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }

    deallocate_file_struct_array(&files, nr_of_files + nr_of_wanted_files);
}

int main(int argc, char *argv[])
{
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK)
    {
        tracker(numtasks, rank);
    }
    else
    {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
