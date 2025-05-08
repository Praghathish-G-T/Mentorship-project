#define _POSIX_C_SOURCE 200809L // For sigaction

 #include <stdio.h>
 #include <stdlib.h>
 #include <signal.h> // For signal handling
 #include <unistd.h> // For pause(), write()
 #include <string.h> // For strlen(), memset()
 #include <microhttpd.h>
 #include "mentorship_data.h"
 #include "api_handler.h" // Contains request_handler

 #define PORT 8080
 #define DATA_FILE "Backend/mentorship_data.json" // Default Data file path

 // --- Global Variables (for signal handler access) ---
 static struct MHD_Daemon *daemon_ptr = NULL;
 static AppData *app_data_ptr = NULL;

 // --- Function defined in mentorship_data.c to set the path ---
 extern void set_data_file_path(const char* path);


 /**
  * @brief Signal handler for graceful shutdown (SIGINT, SIGTERM).
  */
 void handle_sigterm(int signum) {
     (void)signum; // Mark unused

     const char* shutdown_msg = "\nReceived signal, shutting down gracefully...\n";
     // Use write for signal safety, though printf might be okay in simple cases
     write(STDOUT_FILENO, shutdown_msg, strlen(shutdown_msg));

     if (daemon_ptr) {
         const char* stopping_daemon_msg = "Stopping MHD daemon...\n";
         write(STDOUT_FILENO, stopping_daemon_msg, strlen(stopping_daemon_msg));
         MHD_stop_daemon(daemon_ptr);
         daemon_ptr = NULL; // Prevent double-stopping
         const char* stopped_daemon_msg = "MHD daemon stopped.\n";
         write(STDOUT_FILENO, stopped_daemon_msg, strlen(stopped_daemon_msg));
     }

     // Attempt to save data on shutdown
     if (app_data_ptr) {
         const char* saving_data_msg = "Attempting to save data before exit...\n";
         write(STDOUT_FILENO, saving_data_msg, strlen(saving_data_msg));
         // Pass NULL to save_data_to_file to use the globally set path (or its default)
         if (save_data_to_file(app_data_ptr, NULL)) {
             const char* save_success_msg = "Data saved successfully.\n";
             write(STDOUT_FILENO, save_success_msg, strlen(save_success_msg));
         } else {
             const char* save_fail_msg = "Warning: Failed to save data on shutdown.\n";
             write(STDERR_FILENO, save_fail_msg, strlen(save_fail_msg)); // Write warnings to stderr
         }

         const char* freeing_data_msg = "Freeing application data...\n";
         write(STDOUT_FILENO, freeing_data_msg, strlen(freeing_data_msg));
         free_app_data(app_data_ptr); // Frees all linked lists
         app_data_ptr = NULL; // Prevent double-freeing
         const char* freed_data_msg = "Application data freed.\n";
         write(STDOUT_FILENO, freed_data_msg, strlen(freed_data_msg));
     }

     const char* shutdown_complete_msg = "Shutdown complete.\n";
     write(STDOUT_FILENO, shutdown_complete_msg, strlen(shutdown_complete_msg));
     _exit(0); // Use _exit for signal safety (avoids stdio buffers)
 }

 /**
  * @brief Main entry point.
  */
 int main(int argc, char *argv[]) {
     // Allow overriding data file path via command-line argument
     const char* data_file_path = DATA_FILE; // Default
     if (argc > 1) {
         data_file_path = argv[1];
         printf("Using data file path from command line: %s\n", data_file_path);
     } else {
          printf("Using default data file path: %s\n", data_file_path);
     }
     fflush(stdout);

     // Set the global data file path for saving/loading functions
     set_data_file_path(data_file_path);

     printf("Initializing application data...\n"); fflush(stdout);
     app_data_ptr = initialize_app_data(); // Loads data or creates new using the set path
     if (!app_data_ptr) {
         fprintf(stderr, "Fatal Error: Failed to initialize application data. Exiting.\n");
         return 1;
     }
     printf("Application data initialized successfully.\n"); fflush(stdout);

     // Setup signal handling for graceful shutdown (SIGINT = Ctrl+C, SIGTERM = kill)
     struct sigaction sa;
     memset(&sa, 0, sizeof(sa));
     sa.sa_handler = handle_sigterm;
     // Block other signals during handler execution if needed (optional)
     // sigemptyset(&sa.sa_mask);
     // sa.sa_flags = SA_RESTART; // Optional: restart syscalls if interrupted

     if (sigaction(SIGTERM, &sa, NULL) == -1) {
         perror("Error setting SIGTERM handler");
         // Consider if this is fatal; maybe still try to run?
     }
     if (sigaction(SIGINT, &sa, NULL) == -1) {
         perror("Error setting SIGINT handler");
         // Consider if fatal
     }

     printf("Starting MHD daemon on port %d...\n", PORT); fflush(stdout);

     // Start the microhttpd web server
     // MHD_USE_DEBUG can be helpful but verbose
     // MHD_USE_THREAD_PER_CONNECTION or MHD_USE_POLL_INTERNALLY are common choices
     daemon_ptr = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY | MHD_USE_THREAD_PER_CONNECTION, // | MHD_USE_DEBUG,
                                 PORT,
                                 NULL, NULL,        // Connection check callback (optional)
                                 &request_handler,  // The main request handler from api_handler.c
                                 app_data_ptr,      // Pass AppData pointer to the request handler
                                 MHD_OPTION_END);

     if (NULL == daemon_ptr) {
         fprintf(stderr, "Fatal Error: Failed to start MHD daemon on port %d. Check permissions or if port is already in use.\n", PORT);
         free_app_data(app_data_ptr); // Clean up allocated data
         return 1;
     }

     printf("Mentor Dashboard Backend running on http://localhost:%d\n", PORT);
     printf("Press Ctrl+C to stop.\n");
     fflush(stdout);

     // Keep the main thread alive while the daemon runs in background threads
     // pause() waits for a signal to arrive.
     while (1) {
         pause();
     }

     // --- Code below should not be reached due to signal handler exit ---
     printf("Performing cleanup (should not normally be reached)...\n");
     if (daemon_ptr) { MHD_stop_daemon(daemon_ptr); }
     if (app_data_ptr) {
         // Optionally save data one last time if not handled by signal handler
         // save_data_to_file(app_data_ptr, NULL);
         free_app_data(app_data_ptr);
     }
     return 0; // Indicates successful execution, though unlikely to be reached
 }