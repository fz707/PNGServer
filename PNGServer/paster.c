//#include "cURL/main_simple.c"
#include "./../lab1/starter/png_util/crc.c"
#include "./../lab1/starter/png_util/zutil.c" /* for mem_def() and mem_inf() */
#include <curl/curl.h>
#include <dirent.h>
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdio.h>  /* for printf().  man 3 printf */
#include <stdlib.h> /* for exit().    man 3 exit   */
#include <string.h>
#include <string.h> /* for strcat().  man strcat   */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define IMG_URL "http://ece252-1.uwaterloo.ca:2520/image?img=1"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 8000
#define max(a, b)                                                              \
  ({                                                                           \
    __typeof__(a) _a = (a);                                                    \
    __typeof__(b) _b = (b);                                                    \
    _a > _b ? _a : _b;                                                         \
  })

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;

int counter;
typedef struct recv_buf {
  char *buf;       /* memory to hold a copy of received data */
  size_t size;     /* size of valid data in buf in bytes*/
  size_t max_size; /* max capacity of buf in bytes*/
  int seq;         /* >=0 sequence number extracted from http header */
                   /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct thread_args {
  RECV_BUF *slice_array; // Has it's own buffer
  char server;
  char image;
} THREAD_ARGS;
int get_dimension(int seekstart, RECV_BUF index);
int get_dimension_file(int seekstart, FILE *x);
void catpng(RECV_BUF slice_arr[], int n);
u_int32_t ntohl(u_int32_t hostlong);
int crcCheck(int seekstart, int data_size, FILE *y);
int recv_buf_cleanup(RECV_BUF *ptr) {
  if (ptr == NULL) {
    return 1;
  }

  free(ptr->buf);
  ptr->size = 0;
  ptr->max_size = 0;
  return 0;
}

int write_file(const char *path, const void *in, size_t len) {
  FILE *fp = NULL;

  if (path == NULL) {
    fprintf(stderr, "write_file: file name is null!\n");
    return -1;
  }

  if (in == NULL) {
    fprintf(stderr, "write_file: input data is null!\n");
    return -1;
  }

  fp = fopen(path, "wb");
  if (fp == NULL) {
    perror("fopen");
    return -2;
  }

  if (fwrite(in, 1, len, fp) != len) {
    fprintf(stderr, "write_file: imcomplete write!\n");
    return -3;
  }
  return fclose(fp);
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size) {
  void *p = NULL;

  if (ptr == NULL) {
    return 1;
  }

  p = malloc(max_size);
  if (p == NULL) {
    return 2;
  }

  ptr->buf = p;
  ptr->size = 0;
  ptr->max_size = max_size;
  ptr->seq = -1; /* valid seq should be non-negative */
  return 0;
}
// write callback function
// p_recv is a pointer to the recieved libcurl data
// size is the size of the recieved data
//
size_t write_slice(char *p_recv, size_t size, size_t nmemb, void *p_userdata) {
  size_t realsize = size * nmemb;
  RECV_BUF *p = (RECV_BUF *)p_userdata;

  if (p->size + realsize + 1 > p->max_size) { /* hope this rarely happens */
    /* received data is not 0 terminated, add one byte for terminating 0 */
    size_t new_size = p->max_size + max(4000, realsize + 1);
    char *q = realloc(p->buf, new_size);
    if (q == NULL) {
      perror("realloc"); /* out of memory */
      return -1;
    }
    p->buf = q;
    p->max_size = new_size;
  }

  memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
  p->size += realsize;
  p->buf[p->size] = 0;

  return realsize;
}

size_t header_slice(char *p_recv, size_t size, size_t nmemb, void *userdata) {
  int realsize = size * nmemb;
  RECV_BUF *p = userdata;

  if (realsize > strlen(ECE252_HEADER) &&
      strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

    /* extract img sequence number */

    p->seq = atoi(p_recv + strlen(ECE252_HEADER));
  }

  return realsize;
}

// cURL function that will run in every thread, I THINK this will contain all
// the initilization procedure for curl

void *get_slices(void *arg) {

  while (counter != 50) {
    RECV_BUF recv_buf;
    recv_buf_init(&recv_buf, BUF_SIZE);
    THREAD_ARGS *p_in = arg;
    CURL *curl_handle = curl_easy_init();
    // define url to hit
    char img_url[250];
    sprintf(img_url, "ece252-%c.uwaterloo.ca:2520/image?img=%c", p_in->server,
            p_in->image);
    //printf("%s: is the URL\n", img_url);
    if (curl_handle == NULL) {
      fprintf(stderr, "curl_easy_init: returned NULL\n");
      return 1;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, img_url);

    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_slice);
    // pass where to write the retrieved data, this is passed to the callback
    // function we passed above
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);
    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_slice);
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
    }
    if (p_in->slice_array[recv_buf.seq].seq == -1) {
      p_in->slice_array[recv_buf.seq] = recv_buf;
      counter++;
    }
    // recv_buf_cleanup(&recv_buf);
    curl_easy_cleanup(curl_handle);
  }
  return ((void *)NULL);
}

int main(int argc, char **argv) {
  int c = 0;
  int t = 1;
  int n = 1;
  char *str = "option requires an argument";
  RECV_BUF slice_array[50];
  // Initializing array
  for (int i = 0; i < 50; i++) {
    recv_buf_init(&slice_array[i], BUF_SIZE);
  }

  // create global array of structs that will hold all the image slices

  // what we might have to do is pass a struct of callback functions as the
  // parameter into pthread_create(), along with the array? Not sure if we have
  // to do this or can just pass it from global variables? Look into this later

  // gets the command line variables for num of threads and image number
  char url[256];

  if (argc == 1) {
    strcpy(url, IMG_URL);
  } else {
    strcpy(url, argv[1]);
  }
  while ((c = getopt(argc, argv, "t:n:")) != -1) {
    switch (c) {
    case 't':
      t = strtoul(optarg, NULL, 10);
      if (t <= 0) {
        fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
        return -1;
      }
      break;
    case 'n':
      n = strtoul(optarg, NULL, 10);
      if (n <= 0 || n > 3) {
        fprintf(stderr, "%s: %s 1, 2, or 3 -- 'n'\n", argv[0], str);
        return -1;
      }
      break;
    default:
      return -1;
    }
  }
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Allocate space to store the number of threads
  pthread_t *p_tids = malloc(sizeof(pthread_t) * t);
  THREAD_ARGS in_params[t];

  for (int i = 0; i < t; i++) {
    // pass in the array that stores the slices
    in_params[i].slice_array = slice_array;
    // use mod to calculate which server to call
    in_params[i].server = ((i % 3) + 1) + '0';
    in_params[i].image = n + '0';
    pthread_create(p_tids + i, NULL, get_slices, in_params + i);
  }

  for (int i = 0; i < t; i++) {
    pthread_join(p_tids[i], NULL);
    //printf("Thread ID %lu joined.\n", p_tids[i]);
  }

  free(p_tids);
  puts("\n");
  catpng(slice_array, 50);
  for (int i = 0; i < 50; i++) {
    recv_buf_cleanup(&slice_array[i]);
  }

  curl_global_cleanup();
  return 0;
}

// helper function to get either the width or height of an image stored in a buffer struct
int get_dimension(int seekstart, RECV_BUF index) {
  u_int32_t dimension = 0;
  int power = 3;
  unsigned char dim[4];
  memcpy(dim, index.buf + seekstart, 4);
  for (int i = 0; i < 4; i++) {
    dimension += (pow(256, power) * dim[i]);
    power--;
  }
  return dimension;
}

// helper function to get image dimensions from a file
int get_dimension_file(int seekstart, FILE *x) {
  u_int32_t dimension = 0;
  int power = 3;
  unsigned char dim[4];
  fseek(x, seekstart, SEEK_SET);
  fread(dim, sizeof(dim), 1, x);
  for (int i = 0; i < 4; i++) {
    dimension += (pow(256, power) * dim[i]);
    power--;
  }
  return dimension;
}

// helper function to check the validity of the CRC
int crcCheck(int seekstart, int data_size, FILE *y) {
  seekstart += 4;
  unsigned char dim[data_size + 4];
  fseek(y, seekstart, SEEK_SET);
  fread(dim, sizeof(dim), 1, y);
  int calc = crc(dim, data_size + 4);
  return calc;
}

// helper function to concatenate png images stored in an array of buffer structs
void catpng(RECV_BUF slice_arr[], int n) {
  unsigned char old_IHDR[33];
  unsigned char IEND[12];
  unsigned char IDAT_type[4];
  //FILE *fptr; dont think we need this
  unsigned char concat_buffer[5000000];
  memcpy(old_IHDR, slice_arr[0].buf, 33);

  int IDAT_start = 33;
  int IDAT_data_length = get_dimension(IDAT_start, slice_arr[0]);

  memcpy(IDAT_type, slice_arr[0].buf + IDAT_start + 4, 4);
  memcpy(IEND, slice_arr[0].buf + IDAT_data_length + 12 + IDAT_start, 12);
  int IDAT_length_total = 0;
  int height_total = 0;
  int global_dest_holder = 0;
  U64 deflate_length = 0;
  // FOR LOOP STARTS HERE
  for (int i = 0; i < n; ++i) {
    int height = 0;
    int width = 0;
    height = get_dimension(20, slice_arr[i]);
    height_total += height;
    width = get_dimension(16, slice_arr[i]);
    int IDAT_length = (get_dimension(33, slice_arr[i]));
    IDAT_length_total += IDAT_length;

    U8 source_buffer[IDAT_length];
    memcpy(source_buffer, slice_arr[i].buf + IDAT_start + 8, IDAT_length);
    U64 inflate_length = height * ((width * 4) + 1);

    U8 inflate_destination[inflate_length];

    int ret = mem_inf(inflate_destination, &inflate_length, source_buffer,
                      IDAT_length);
    if (ret == 0) { /* success */
      memcpy(concat_buffer + global_dest_holder, inflate_destination,
             inflate_length);
      global_dest_holder += inflate_length;
    }
  }

  unsigned char deflate_dest_buffer[IDAT_length_total];
  // DEFLATE
  int ret_deflate = mem_def(deflate_dest_buffer, &deflate_length, concat_buffer,
                            global_dest_holder, Z_DEFAULT_COMPRESSION);
  if (ret_deflate == 0) { /* success */
    // puts("Common W");
  } else { /* failure */
    fprintf(stderr, "mem_def failed. ret = %d.\n", ret_deflate);
    return; //ret_deflate;
  }
  //printf("%d\n", height_total);
  height_total = ntohl(height_total);
  memcpy(old_IHDR + 20, &height_total, sizeof(height_total));
  FILE *writer;
  writer = fopen("all.png", "wb+");
  fwrite(old_IHDR, sizeof(old_IHDR), 1, writer);
  int IHDR_start = 8; // Skip the png signature
  int IHDR_length = get_dimension_file(IHDR_start, writer); // Gets data length
  u_int32_t crcval = crcCheck(IHDR_start, IHDR_length, writer);
  int IHDR_crc = IHDR_start + 8 + IHDR_length; // Get to IHDR start
  // REWRITE THE CRC CHUNK TO FIX ERROR
  u_int32_t n_crcval = ntohl(crcval);
  fseek(writer, IHDR_crc, SEEK_SET);
  fwrite(&n_crcval, sizeof(n_crcval), 1, writer);

  fseek(writer, 0, SEEK_SET);

  // int wid = get_dimension(16, writer);
  // int height1 = get_dimension(20, writer);
  // printf("\n the width is%d", wid);
  // printf("\n the height is%d\n", height1);
  fseek(writer, 33, SEEK_SET);
  // SEEK TO BEGINNING OF IDAT^
  // Writes IDAT DATA FIELD Length
  u_int32_t shifted_def = ntohl((unsigned int)deflate_length << 32);
  fwrite(&shifted_def, sizeof(shifted_def), 1, writer);
  // Prints out Signature and IHDR
  // fseek(writer, 0, SEEK_SET);
  // fread(ihdr_and_signature, sizeof(ihdr_and_signature), 1, writer);
  // // for (int i = 0; i < sizeof(ihdr_and_signature); i++) {
  // //   printf("%02x,", ihdr_and_signature[i]);
  // // }
  // Writes IDAT Type
  fseek(writer, 37, SEEK_SET);
  fwrite(IDAT_type, sizeof(IDAT_type), 1, writer);
  // Prints out Length and Type of IDAT
  // fseek(writer, 33, SEEK_SET);
  // fread(IDAT_length_type_buffer, sizeof(IDAT_length_type_buffer), 1, writer);
  // // for (int i = 0; i < sizeof(IDAT_length_type_buffer); i++) {
  // //   printf("%02x,", IDAT_length_type_buffer[i]);
  // // }
  // // writes deflated data,CHECK A LOT

  fwrite(deflate_dest_buffer, deflate_length, 1, writer);

  // compute new crc for IDAT
  int IDAT_length = get_dimension_file(IDAT_start, writer); // Gets data length
  crcval = crcCheck(IDAT_start, IDAT_length, writer);
  n_crcval = ntohl(crcval);
  // int IDAT_crc = IDAT_start + 8 + IDAT_length; // Get to IDAT crc OFFSET
  // printf("\nCRC val: %02x ", crcval);
  // writes the computed crc val for new IDAT chunk
  fwrite(&n_crcval, sizeof(n_crcval), 1, writer); // PRINT IT OUTs

  // writes the IEND
  // for (int i = 0; i < sizeof(IEND); i++) {
  //   printf("%02x,", IEND[i]);
  // }
  fwrite(IEND, sizeof(IEND), 1, writer);
}