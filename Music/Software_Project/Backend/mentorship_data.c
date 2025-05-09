#define _XOPEN_SOURCE 700 // For strptime
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // For strcasecmp (POSIX)
#include <time.h>
#include <errno.h>
#include <ctype.h> // For isspace, tolower
#include "mentorship_data.h"
#include <cjson/cJSON.h>

// Use the specific path defined in main.c or passed to functions
// We'll rely on the filename parameter passed to save/load functions
// #define DATA_FILE "Backend/mentorship_data.json" // Default data file - Removed hardcoding here

// ========================================================================== //
//                            UTILITY FUNCTIONS                               //
// ========================================================================== //

/**
 * @brief Safely duplicates a string using malloc. Handles NULL input. Caller frees.
 */
char* safe_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* new_s = malloc(len);
    if (!new_s) {
        perror("safe_strdup: malloc failed");
        return NULL;
    }
    memcpy(new_s, s, len);
    return new_s;
}

/**
 * @brief Converts an IssuePriority enum to string.
 */
const char* priority_to_string(IssuePriority p) {
    switch (p) {
        case PRIORITY_LOW: return "Low";
        case PRIORITY_MEDIUM: return "Medium";
        case PRIORITY_HIGH: return "High";
        default: return "Unknown";
    }
}

/**
 * @brief Converts a string to IssuePriority enum (case-insensitive).
 */
IssuePriority string_to_priority(const char* s) {
    if (!s) return PRIORITY_MEDIUM;
    if (strcasecmp(s, "Low") == 0) return PRIORITY_LOW;
    if (strcasecmp(s, "High") == 0) return PRIORITY_HIGH;
    // Default to Medium if not Low or High
    return PRIORITY_MEDIUM;
}

/**
 * @brief Converts an IssueStatus enum to string.
 */
const char* status_to_string(IssueStatus s) {
    switch (s) {
        case STATUS_OPEN: return "Open";
        case STATUS_IN_PROGRESS: return "In Progress";
        case STATUS_RESOLVED: return "Resolved";
        default: return "Unknown";
    }
}

/**
 * @brief Converts a string to IssueStatus enum (case-insensitive).
 */
IssueStatus string_to_status(const char* s) {
    if (!s) return STATUS_OPEN; // Default to Open
    if (strcasecmp(s, "In Progress") == 0) return STATUS_IN_PROGRESS;
    if (strcasecmp(s, "Resolved") == 0) return STATUS_RESOLVED;
    // Default to Open if not In Progress or Resolved
    return STATUS_OPEN;
}

/**
 * @brief Converts a UserRole enum to string.
 */
const char* role_to_string(UserRole r) {
    switch (r) {
        case ROLE_MENTOR: return "mentor";
        case ROLE_MENTEE: return "mentee";
        default: return "unknown";
    }
}

/**
 * @brief Converts a string to UserRole enum (case-insensitive).
 */
UserRole string_to_role(const char* s) {
    if (!s) return ROLE_MENTEE; // Default? Or maybe an error state?
    if (strcasecmp(s, "mentor") == 0) return ROLE_MENTOR;
    // Default to mentee if not mentor
    return ROLE_MENTEE;
}

// ========================================================================== //
//                             NOTE FUNCTIONS                                 //
// ========================================================================== //

/**
 * @brief Adds a new note to the beginning of a linked list. Allocates memory.
 */
Note* add_note(Note** head_ref, const char* text) {
    if (!head_ref || !text) {
        fprintf(stderr, "add_note: Error - NULL head_ref or text provided.\n");
        return NULL;
    }
    Note* new_note = malloc(sizeof(Note));
    if (!new_note) {
        perror("add_note: malloc failed for Note struct");
        return NULL;
    }
    new_note->text = safe_strdup(text);
    if (!new_note->text && strlen(text) > 0) { // Check strdup failure only if text wasn't empty
        free(new_note);
        return NULL;
    }
    new_note->timestamp = time(NULL);
    new_note->next = *head_ref;
    *head_ref = new_note;
    return new_note;
}

/**
 * @brief Frees all notes in a linked list.
 */
void free_notes(Note* head) {
    Note* current = head;
    Note* next_node;
    while (current != NULL) {
        next_node = current->next;
        free(current->text);
        free(current);
        current = next_node;
    }
}

// ========================================================================== //
//                            MENTEE FUNCTIONS                                //
// ========================================================================== //

/**
 * @brief Adds a new mentee. Assigns ID, duplicates strings. DOES NOT SAVE.
 */
Mentee* add_mentee(AppData* data, const char* name, const char* subject, const char* email) {
    if (!data || !name || !subject) {
         fprintf(stderr, "add_mentee: Error - NULL data, name, or subject provided.\n");
         return NULL;
    }

    Mentee* new_mentee = malloc(sizeof(Mentee));
    if (!new_mentee) {
        perror("add_mentee: malloc failed for Mentee struct");
        return NULL;
    }

    new_mentee->name = safe_strdup(name);
    new_mentee->subject = safe_strdup(subject);
    new_mentee->email = safe_strdup(email); // Handles NULL email

    // Check for allocation failure *after* attempting all strdups
    if (!new_mentee->name || !new_mentee->subject || (email && strlen(email) > 0 && !new_mentee->email)) {
        fprintf(stderr, "add_mentee: Failed to duplicate one or more input strings.\n");
        free(new_mentee->name); // safe to free NULL
        free(new_mentee->subject);
        free(new_mentee->email);
        free(new_mentee);
        return NULL;
    }

    new_mentee->id = data->next_mentee_id++;
    new_mentee->general_notes = NULL;
    new_mentee->next = data->mentees_head;
    data->mentees_head = new_mentee;

    // Saving should be handled explicitly by the caller (e.g., after adding mentee + user)
    // if (!save_data_to_file(data, DATA_FILE)) { ... }

    return new_mentee;
}

/**
 * @brief Finds a mentee by their unique ID.
 */
Mentee* find_mentee_by_id(const AppData* data, int id) {
    // --- ADDED LOGGING ---
    printf("      [find_mentee_by_id] Searching for ID: %d\n", id); fflush(stdout);
    // --- END LOGGING ---

    if (!data || id <= 0) {
        // --- ADDED LOGGING ---
        printf("      [find_mentee_by_id] Invalid input (data=%p, id=%d)\n", (void*)data, id); fflush(stdout);
        // --- END LOGGING ---
        return NULL;
    }
    Mentee* current = data->mentees_head;
    int count = 0;
    while (current != NULL) {
        // --- ADDED LOGGING ---
        printf("      [find_mentee_by_id] Checking mentee node %d -> ID: %d (Name: %s)\n", count, current->id, current->name ? current->name : "N/A"); fflush(stdout);
        // --- END LOGGING ---
        if (current->id == id) {
            // --- ADDED LOGGING ---
            printf("      [find_mentee_by_id] Match found for ID: %d\n", id); fflush(stdout);
            // --- END LOGGING ---
            return current;
        }
        current = current->next;
        count++;
    }
    // --- ADDED LOGGING ---
    printf("      [find_mentee_by_id] ID: %d not found after checking %d nodes.\n", id, count); fflush(stdout);
    // --- END LOGGING ---
    return NULL;
}


/**
 * @brief Finds a mentee by their name (case-sensitive).
 */
Mentee* find_mentee_by_name(const AppData* data, const char* name) {
     if (!data || !name) return NULL;
     Mentee* current = data->mentees_head;
     while (current != NULL) {
         if (current->name && strcmp(current->name, name) == 0) {
             return current;
         }
         current = current->next;
     }
     return NULL;
}

/**
 * @brief Deletes a mentee by ID. Frees memory. DOES NOT SAVE.
 * TODO: Caller should handle deleting associated User account.
 */
int delete_mentee(AppData* data, int id) {
    if (!data || !data->mentees_head || id <= 0) {
         fprintf(stderr,"delete_mentee: List empty, data is NULL, or invalid ID (%d).\n", id);
        return 0; // Indicate failure: not found or invalid input
    }

    Mentee* current = data->mentees_head;
    Mentee* prev = NULL;

    // Find the mentee node
    while (current != NULL && current->id != id) {
        prev = current;
        current = current->next;
    }

    // Mentee not found
    if (current == NULL) {
        fprintf(stderr, "delete_mentee: Mentee with ID %d not found.\n", id);
        return 0; // Indicate failure: not found
    }

    // Unlink the node from the list
    if (prev == NULL) { // Node to delete is the head
        data->mentees_head = current->next;
    } else { // Node is in the middle or end
        prev->next = current->next;
    }

    // TODO: Find and delete user with role MENTEE and associated_id == id (handled by caller?)

    // Free the memory associated with the deleted mentee
    free(current->name);
    free(current->subject);
    free(current->email);
    free_notes(current->general_notes);
    free(current); // Free the struct itself

    // Saving should be handled explicitly by the caller after successful deletion
    // if (!save_data_to_file(data, DATA_FILE)) { ... }

    return 1; // Indicate success
}


/**
 * @brief Adds a general note to a specific mentee. Does NOT save automatically.
 */
void add_mentee_note(Mentee* mentee, const char* note_text) {
    if (!mentee || !note_text) {
         fprintf(stderr, "add_mentee_note: Error - NULL mentee or note_text provided.\n");
         return;
    }
    if (!add_note(&(mentee->general_notes), note_text)) {
         fprintf(stderr, "Warning: Failed to add general note to mentee ID %d.\n", mentee->id);
    }
    // Saving is handled by caller if needed
}

/**
 * @brief Frees all Mentee structs and their data in a list. (Internal use for cleanup).
 */
void free_mentees(Mentee* head) {
    Mentee* current = head;
    Mentee* next_node;
    while (current != NULL) {
        next_node = current->next;
        free(current->name);
        free(current->subject);
        free(current->email);
        free_notes(current->general_notes);
        free(current);
        current = next_node;
    }
}


// ========================================================================== //
//                            MEETING FUNCTIONS                               //
// ========================================================================== //

/**
 * @brief Adds a new meeting. Assigns ID, duplicates strings. DOES NOT SAVE.
 */
Meeting* add_meeting(AppData* data, int mentee_id, const char* mentee_name, const char* date_str, const char* time_str, int duration, const char* notes) {
    if (!data || mentee_id <= 0 || !mentee_name || !date_str || !time_str || duration <= 0) {
         fprintf(stderr, "add_meeting: Error - Invalid input data provided.\n");
         return NULL;
    }

    Meeting* new_meeting = malloc(sizeof(Meeting));
    if (!new_meeting) {
        perror("add_meeting: malloc failed");
        return NULL;
    }

    new_meeting->mentee_name = safe_strdup(mentee_name);
    new_meeting->date_str = safe_strdup(date_str);
    new_meeting->time_str = safe_strdup(time_str);
    new_meeting->notes = safe_strdup(notes); // Handles NULL notes

     if (!new_meeting->mentee_name || !new_meeting->date_str || !new_meeting->time_str || (notes && strlen(notes) > 0 && !new_meeting->notes)) {
        fprintf(stderr, "add_meeting: Failed to duplicate one or more input strings.\n");
        free(new_meeting->mentee_name);
        free(new_meeting->date_str);
        free(new_meeting->time_str);
        free(new_meeting->notes);
        free(new_meeting);
        return NULL;
    }

    new_meeting->id = data->next_meeting_id++;
    new_meeting->mentee_id = mentee_id;
    new_meeting->duration_minutes = duration;
    new_meeting->next = data->meetings_head;
    data->meetings_head = new_meeting;

    // Saving handled by caller
    // if (!save_data_to_file(data, DATA_FILE)) { ... }

    return new_meeting;
}

/**
 * @brief Finds a meeting by its unique ID.
 */
Meeting* find_meeting_by_id(const AppData* data, int id) {
    if (!data || id <= 0) return NULL;
    Meeting* current = data->meetings_head;
    while (current != NULL) {
        if (current->id == id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * @brief Updates date and time of a meeting. Frees old strings. Does NOT save automatically.
 */
int update_meeting(Meeting* meeting, const char* new_date_str, const char* new_time_str) {
    if (!meeting || !new_date_str || !new_time_str) {
        fprintf(stderr, "update_meeting: Error - NULL input provided.\n");
        return 0; // Indicate failure
    }

    char* temp_date = safe_strdup(new_date_str);
    char* temp_time = safe_strdup(new_time_str);

    if (!temp_date || !temp_time) {
        free(temp_date); // free(NULL) is safe
        free(temp_time);
        fprintf(stderr, "update_meeting: Failed to duplicate new date/time strings.\n");
        return 0; // Indicate failure
    }

    // Free old strings only after successful duplication of new ones
    free(meeting->date_str);
    free(meeting->time_str);

    meeting->date_str = temp_date;
    meeting->time_str = temp_time;

    // Saving is handled by caller
    return 1; // Indicate success
}


/**
 * @brief Deletes a meeting by ID. Frees memory. DOES NOT SAVE.
 */
int delete_meeting(AppData* data, int meeting_id) {
    if (!data || !data->meetings_head || meeting_id <= 0) {
        fprintf(stderr,"delete_meeting: List empty, data is NULL, or invalid ID (%d).\n", meeting_id);
        return 0; // Failure: Invalid input or empty list
    }

    Meeting* current = data->meetings_head;
    Meeting* prev = NULL;

    // Find the meeting node
    while (current != NULL && current->id != meeting_id) {
        prev = current;
        current = current->next;
    }

    // Meeting not found
    if (current == NULL) {
        fprintf(stderr, "delete_meeting: Meeting with ID %d not found.\n", meeting_id);
        return 0; // Failure: Not found
    }

    // Unlink the node
    if (prev == NULL) { // Head node
        data->meetings_head = current->next;
    } else { // Middle or end node
        prev->next = current->next;
    }

    // Free the memory
    free(current->mentee_name);
    free(current->date_str);
    free(current->time_str);
    free(current->notes);
    free(current);

    // Saving handled by caller
    // if (!save_data_to_file(data, DATA_FILE)) { ... }

    return 1; // Success
}


/**
 * @brief Frees all Meeting structs and their data in a list. (Internal use for cleanup).
 */
void free_meetings(Meeting* head) {
    Meeting* current = head;
    Meeting* next_node;
    while (current != NULL) {
        next_node = current->next;
        free(current->mentee_name);
        free(current->date_str);
        free(current->time_str);
        free(current->notes);
        free(current);
        current = next_node;
    }
}

// ========================================================================== //
//                             ISSUE FUNCTIONS                                //
// ========================================================================== //

/**
 * @brief Adds a new issue. Assigns ID, duplicates strings. DOES NOT SAVE.
 */
Issue* add_issue(AppData* data, int mentee_id, const char* mentee_name, const char* description, const char* date_reported, IssuePriority priority) {
     if (!data || mentee_id <= 0 || !mentee_name || !description || !date_reported) {
         fprintf(stderr, "add_issue: Error - Invalid input data provided.\n");
         return NULL;
     }

    Issue* new_issue = malloc(sizeof(Issue));
    if (!new_issue) {
        perror("add_issue: malloc failed");
        return NULL;
    }

    new_issue->mentee_name = safe_strdup(mentee_name);
    new_issue->description = safe_strdup(description);
    new_issue->date_reported_str = safe_strdup(date_reported);

    if (!new_issue->mentee_name || !new_issue->description || !new_issue->date_reported_str) {
        fprintf(stderr, "add_issue: Failed to duplicate input strings.\n");
        free(new_issue->mentee_name);
        free(new_issue->description);
        free(new_issue->date_reported_str);
        free(new_issue);
        return NULL;
    }

    new_issue->id = data->next_issue_id++;
    new_issue->mentee_id = mentee_id;
    new_issue->priority = priority;
    new_issue->status = STATUS_OPEN; // New issues always start as Open
    new_issue->response_notes = NULL;
    new_issue->next = data->issues_head;
    data->issues_head = new_issue;

    // Saving handled by caller
    // if (!save_data_to_file(data, DATA_FILE)) { ... }

    return new_issue;
}

/**
 * @brief Finds an issue by its unique ID.
 */
Issue* find_issue_by_id(const AppData* data, int id) {
    if (!data || id <= 0) return NULL;
    Issue* current = data->issues_head;
    while (current != NULL) {
        if (current->id == id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * @brief Updates issue status and optionally adds a note. Does NOT save automatically.
 */
int update_issue_status(Issue* issue, IssueStatus new_status, const char* note_text) {
    if (!issue) {
        fprintf(stderr, "update_issue_status: Error - NULL issue provided.\n");
        return 0; // Failure
    }

    issue->status = new_status;

    // Add note only if text is provided and not empty
    if (note_text && strlen(note_text) > 0) {
        if (!add_note(&(issue->response_notes), note_text)) {
            // Log warning but don't necessarily fail the status update
            fprintf(stderr, "Warning: Failed to add response note while updating issue %d status.\n", issue->id);
        }
    }

    // Saving is handled by caller
    return 1; // Success
}


/**
 * @brief Frees all Issue structs and their data in a list. (Internal use for cleanup).
 */
void free_issues(Issue* head) {
    Issue* current = head;
    Issue* next_node;
    while (current != NULL) {
        next_node = current->next;
        free(current->mentee_name);
        free(current->description);
        free(current->date_reported_str);
        free_notes(current->response_notes); // Free associated notes
        free(current);
        current = next_node;
    }
}

// ========================================================================== //
//                             USER FUNCTIONS                                 //
// ========================================================================== //

/**
 * @brief Adds a new user. Assigns ID, duplicates strings. DOES NOT SAVE.
 * !! WARNING: Stores plain text password !!
 */
User* add_user(AppData* data, const char* username, const char* password, UserRole role, int associated_id) {
    if (!data || !username || !password) {
        fprintf(stderr, "add_user: Error - NULL data, username, or password provided.\n");
        return NULL;
    }
    // Check if username already exists (case-sensitive)
    if (find_user_by_username(data, username)) {
        fprintf(stderr, "add_user: Error - Username '%s' already exists.\n", username);
        return NULL;
    }

    User* new_user = malloc(sizeof(User));
    if (!new_user) {
        perror("add_user: malloc failed");
        return NULL;
    }

    new_user->username = safe_strdup(username);
    // !! Storing plain text password - Highly Insecure !! Replace with hashing.
    new_user->password = safe_strdup(password);

    if (!new_user->username || !new_user->password) {
        fprintf(stderr, "add_user: Failed to duplicate username/password strings.\n");
        free(new_user->username);
        free(new_user->password);
        free(new_user);
        return NULL;
    }

    new_user->id = data->next_user_id++;
    new_user->role = role;
    new_user->associated_id = associated_id; // Can be 0 for admin/mentor not linked to a specific mentee record
    new_user->next = data->users_head;
    data->users_head = new_user;

    // Save data handled by caller
    // if (!save_data_to_file(data, DATA_FILE)) { ... }
    return new_user;
}

/**
 * @brief Finds a user by username (case-sensitive).
 */
User* find_user_by_username(const AppData* data, const char* username) {
    if (!data || !username) return NULL;
    User* current = data->users_head;
    while (current != NULL) {
        if (current->username && strcmp(current->username, username) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * @brief Verifies username/password. Returns User* if valid, NULL otherwise.
 * !! WARNING: Compares plain text passwords - Highly Insecure !!
 */
User* verify_user_password(const AppData* data, const char* username, const char* password) {
    if (!data || !username || !password) return NULL;
    User* user = find_user_by_username(data, username);
    if (!user) {
        printf("[AUTH DEBUG] User '%s' not found.\n", username); fflush(stdout);
        return NULL; // User not found
    }

    // !! Plain text comparison - Insecure !! Replace with hash verification.
    if (user->password && strcmp(user->password, password) == 0) {
        printf("[AUTH DEBUG] Password match for user '%s'.\n", username); fflush(stdout);
        return user; // Match
    }

    printf("[AUTH DEBUG] Password mismatch for user '%s'.\n", username); fflush(stdout);
    return NULL; // Password mismatch
}

/**
 * @brief Frees all User structs and their data in a list. (Internal use for cleanup).
 */
void free_users(User* head) {
    User* current = head;
    User* next_node;
    while (current != NULL) {
        next_node = current->next;
        free(current->username);
        free(current->password); // Free the plain text password
        free(current);
        current = next_node;
    }
}

// ========================================================================== //
//                      INITIALIZATION / CLEANUP FUNCTIONS                    //
// ========================================================================== //

// Global variable to hold the default data file path, potentially set by main
static const char* global_data_file_path = "Backend/mentorship_data.json"; // Default path

/**
 * @brief Sets the global path for the data file. Should be called once at startup.
 */
void set_data_file_path(const char* path) {
    if (path) {
        global_data_file_path = path;
         printf("Data file path set to: %s\n", global_data_file_path); fflush(stdout);
    }
}

/**
 * @brief Initializes AppData. Loads from file or creates new with defaults.
 */
AppData* initialize_app_data() {
    // Use the global path set by set_data_file_path (or the default)
    printf("Attempting to load data from: %s\n", global_data_file_path); fflush(stdout);
    AppData* data = load_data_from_file(global_data_file_path);
    if (data) {
        printf("Successfully loaded data from %s\n", global_data_file_path); fflush(stdout);
        return data;
    }

    // If loading failed, create a new structure
    printf("Initializing new data structure (load failed or file not found at %s).\n", global_data_file_path); fflush(stdout);
    data = malloc(sizeof(AppData));
    if (!data) {
        perror("initialize_app_data: malloc failed");
        return NULL;
    }

    // Initialize empty lists and IDs
    data->mentees_head = NULL;
    data->meetings_head = NULL;
    data->issues_head = NULL;
    data->users_head = NULL;
    data->next_mentee_id = 1;
    data->next_meeting_id = 1;
    data->next_issue_id = 1;
    data->next_user_id = 1; // Start user IDs from 1

    // Create default users only if creating a brand new data structure
    // Do NOT save immediately here, let the caller handle the first save if needed.
    printf("Creating default users (admin/mentor, user/mentee) for new data file.\n"); fflush(stdout);
    // Add mentor (admin/password) - associated_id 0 indicates no specific mentee link
    add_user(data, "admin", "password", ROLE_MENTOR, 0); // INSECURE! add_user doesn't save
    // Add mentee (user/password) - Needs a corresponding mentee record eventually, associate with 0 for now? Or 1?
    // Let's associate with 0, assuming no default mentee record exists yet.
    add_user(data, "user", "password", ROLE_MENTEE, 0); // INSECURE! User needs to be associated later if needed

    // IMPORTANT: Do not save here. Let the main application logic or API decide when to save initially.
    // Saving here might overwrite an existing file unintentionally if loading failed for other reasons.

    return data;
}


/**
 * @brief Frees all memory associated with the AppData structure.
 */
void free_app_data(AppData* data) {
    if (!data) return;
    printf("Freeing application data...\n"); fflush(stdout);
    free_mentees(data->mentees_head);
    free_meetings(data->meetings_head);
    free_issues(data->issues_head);
    free_users(data->users_head);
    free(data);
    printf("Application data freed.\n"); fflush(stdout);
}


// ========================================================================== //
//                       PERSISTENCE FUNCTIONS (JSON)                         //
// ========================================================================== //

// Forward declarations for JSON helpers (implementation in json_helpers.c)
// Assuming these are correctly defined in json_helpers.c/h
extern cJSON* notes_to_json_array(const Note* head);
extern Note* json_array_to_notes(const cJSON* json_array);
extern cJSON* mentee_to_json(const Mentee* mentee);
extern cJSON* meeting_to_json(const Meeting* meeting);
extern cJSON* issue_to_json(const Issue* issue);
extern cJSON* user_to_json(const User* user);

/**
 * @brief Saves the entire application state (including users) to JSON file.
 * Uses the filename provided, defaulting to the global path if NULL.
 */
int save_data_to_file(const AppData* data, const char* filename) {
    const char* path_to_use = filename ? filename : global_data_file_path;
    printf("Attempting to save data to: %s\n", path_to_use); fflush(stdout);

    if (!data || !path_to_use) {
        fprintf(stderr, "save_data_to_file: Error - NULL data or filename provided.\n");
        return 0; // Failure
    }
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        fprintf(stderr, "save_data_to_file: Failed to create root JSON object.\n");
        return 0; // Failure
    }

    int success = 1; // Assume success initially

    // --- Save Metadata (Counters) ---
    if (!cJSON_AddNumberToObject(root, "next_mentee_id", data->next_mentee_id) ||
        !cJSON_AddNumberToObject(root, "next_meeting_id", data->next_meeting_id) ||
        !cJSON_AddNumberToObject(root, "next_issue_id", data->next_issue_id) ||
        !cJSON_AddNumberToObject(root, "next_user_id", data->next_user_id))
    {
        fprintf(stderr, "save_data_to_file: Failed to add metadata to JSON.\n");
        success = 0; goto cleanup_json; // Use goto for cleanup on failure
    }

    // --- Save Mentees ---
    cJSON* mentees_a = cJSON_CreateArray();
    if (!mentees_a) { success = 0; goto cleanup_json; }
    Mentee* cm = data->mentees_head;
    while (cm) {
        cJSON* mo = mentee_to_json(cm); // mentee_to_json is in json_helpers.c
        if (!mo || !cJSON_AddItemToArray(mentees_a, mo)) {
            fprintf(stderr, "save_data_to_file: Failed to add mentee %d to JSON array.\n", cm->id);
            cJSON_Delete(mo); success = 0; goto cleanup_mentees_array; // Clean up partially added array item
        }
        cm = cm->next;
    }
    if (!cJSON_AddItemToObject(root, "mentees", mentees_a)) {
         success = 0; goto cleanup_mentees_array;
    }
    // Don't goto cleanup_mentees_array from here if AddItemToObject fails,
    // as the array is now owned by root. Let root cleanup handle it.
    if (!cJSON_GetObjectItem(root, "mentees")) { // Check if add failed silently
        fprintf(stderr, "save_data_to_file: Failed to add mentees array to root JSON.\n");
        success = 0; goto cleanup_json;
    }


    // --- Save Meetings ---
    cJSON* meetings_a = cJSON_CreateArray();
    if (!meetings_a) { success = 0; goto cleanup_json; }
    Meeting* cmeet = data->meetings_head;
    while (cmeet) {
        cJSON* meeto = meeting_to_json(cmeet); // meeting_to_json is in json_helpers.c
        if (!meeto || !cJSON_AddItemToArray(meetings_a, meeto)) {
            fprintf(stderr, "save_data_to_file: Failed to add meeting %d to JSON array.\n", cmeet->id);
            cJSON_Delete(meeto); success = 0; goto cleanup_meetings_array;
        }
        cmeet = cmeet->next;
    }
    if (!cJSON_AddItemToObject(root, "meetings", meetings_a)){
         success = 0; goto cleanup_meetings_array; // Array not owned by root yet
    }
     if (!cJSON_GetObjectItem(root, "meetings")) { // Check if add failed silently
        fprintf(stderr, "save_data_to_file: Failed to add meetings array to root JSON.\n");
        success = 0; goto cleanup_json;
    }

    // --- Save Issues ---
    cJSON* issues_a = cJSON_CreateArray();
     if (!issues_a) { success = 0; goto cleanup_json; }
    Issue* ciss = data->issues_head;
    while (ciss) {
        cJSON* isso = issue_to_json(ciss); // issue_to_json is in json_helpers.c
        if (!isso || !cJSON_AddItemToArray(issues_a, isso)) {
             fprintf(stderr, "save_data_to_file: Failed to add issue %d to JSON array.\n", ciss->id);
            cJSON_Delete(isso); success = 0; goto cleanup_issues_array;
        }
        ciss = ciss->next;
    }
     if (!cJSON_AddItemToObject(root, "issues", issues_a)) {
         success = 0; goto cleanup_issues_array; // Array not owned by root yet
     }
      if (!cJSON_GetObjectItem(root, "issues")) { // Check if add failed silently
        fprintf(stderr, "save_data_to_file: Failed to add issues array to root JSON.\n");
        success = 0; goto cleanup_json;
    }

    // --- Save Users ---
    cJSON* users_a = cJSON_CreateArray();
     if (!users_a) { success = 0; goto cleanup_json; }
    User* cu = data->users_head;
    while (cu) {
        cJSON* usero = user_to_json(cu); // user_to_json is in json_helpers.c
        if (!usero || !cJSON_AddItemToArray(users_a, usero)) {
            fprintf(stderr, "save_data_to_file: Failed to add user %d to JSON array.\n", cu->id);
            cJSON_Delete(usero); success = 0; goto cleanup_users_array;
        }
        cu = cu->next;
    }
    if (!cJSON_AddItemToObject(root, "users", users_a)) {
         success = 0; goto cleanup_users_array; // Array not owned by root yet
    }
     if (!cJSON_GetObjectItem(root, "users")) { // Check if add failed silently
        fprintf(stderr, "save_data_to_file: Failed to add users array to root JSON.\n");
        success = 0; goto cleanup_json;
    }


    // --- Write JSON to File ---
    // Use cJSON_PrintBuffered for potentially better performance? Or just cJSON_Print.
    char* json_string = cJSON_Print(root);
    if (!json_string) {
        fprintf(stderr, "save_data_to_file: Failed to print JSON to string.\n");
        success = 0; goto cleanup_json;
    }

    FILE* fp = fopen(path_to_use, "w");
    if (!fp) {
        perror("save_data_to_file: fopen failed");
        free(json_string); // Free the string if file open failed
        success = 0; goto cleanup_json;
    }

    if (fprintf(fp, "%s", json_string) < 0) {
        perror("save_data_to_file: fprintf failed");
        success = 0; // Mark failure but still try to close/free
    }

    fclose(fp);
    free(json_string); // Free the string after writing

    if (success) {
        printf("Data successfully saved to %s.\n", path_to_use); fflush(stdout);
    } else {
         fprintf(stderr, "Error occurred during saving to %s.\n", path_to_use); fflush(stderr);
    }

    goto cleanup_json; // Go to final cleanup

// --- Cleanup Labels ---
cleanup_users_array:
    cJSON_Delete(users_a);
    goto cleanup_json;
cleanup_issues_array:
    cJSON_Delete(issues_a);
    goto cleanup_json;
cleanup_meetings_array:
    cJSON_Delete(meetings_a);
    goto cleanup_json;
cleanup_mentees_array:
    cJSON_Delete(mentees_a);
    goto cleanup_json;

cleanup_json:
    cJSON_Delete(root); // Safely deletes root and all attached items
    return success;
}


/**
 * @brief Loads application state (including users) from JSON file.
 * Returns NULL on failure (file not found, parse error, memory error).
 */
AppData* load_data_from_file(const char* filename) {
    if (!filename) {
        fprintf(stderr, "load_data_from_file: Error - NULL filename provided.\n");
        return NULL;
    }
    printf("Loading data from file: %s\n", filename); fflush(stdout);

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        // Don't print perror if file simply doesn't exist (ENOENT)
        if (errno != ENOENT) {
            perror("load_data_from_file: fopen failed");
        } else {
             printf("load_data_from_file: File not found (errno=%d).\n", errno); fflush(stdout);
        }
        return NULL; // Indicate failure (file not found or other error)
    }

    // Read file content
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
     // Check for read error or empty file
    if (size <= 0 && ferror(fp)) {
         perror("load_data_from_file: ftell failed or file empty/error");
         fclose(fp);
         return NULL;
    }
     if (size == 0){ // Handle empty file case
         printf("load_data_from_file: File is empty.\n"); fflush(stdout);
         fclose(fp);
         return NULL; // Treat empty file as load failure for now
     }
    rewind(fp); // Same as fseek(fp, 0, SEEK_SET);

    char* buffer = malloc(size + 1);
    if (!buffer) {
        perror("load_data_from_file: malloc failed for buffer");
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, size, fp);
    fclose(fp); // Close file immediately after reading

    if (bytes_read != (size_t)size) {
        fprintf(stderr, "load_data_from_file: fread failed (read %zu bytes, expected %ld).\n", bytes_read, size);
        free(buffer);
        return NULL;
    }
    buffer[bytes_read] = '\0'; // Null-terminate the buffer

    // Parse JSON
    cJSON* root = cJSON_ParseWithLength(buffer, bytes_read);
    free(buffer); // Free buffer immediately after parsing
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        fprintf(stderr, "load_data_from_file: JSON Parse Error near: %s\n", error_ptr ? error_ptr : "(unknown)");
        return NULL;
    }

    // --- Create AppData structure ---
    AppData* data = malloc(sizeof(AppData));
    if (!data) {
        perror("load_data_from_file: malloc failed for AppData");
        cJSON_Delete(root);
        return NULL;
    }
    // Initialize heads to NULL before populating
    data->mentees_head = NULL;
    data->meetings_head = NULL;
    data->issues_head = NULL;
    data->users_head = NULL;

    // --- Load Metadata (Counters) ---
    // Provide defaults if items are missing or invalid
    cJSON *item;
    item = cJSON_GetObjectItemCaseSensitive(root, "next_mentee_id");
    data->next_mentee_id = (cJSON_IsNumber(item) && item->valuedouble >= 1) ? (int)item->valuedouble : 1;
    item = cJSON_GetObjectItemCaseSensitive(root, "next_meeting_id");
    data->next_meeting_id = (cJSON_IsNumber(item) && item->valuedouble >= 1) ? (int)item->valuedouble : 1;
    item = cJSON_GetObjectItemCaseSensitive(root, "next_issue_id");
    data->next_issue_id = (cJSON_IsNumber(item) && item->valuedouble >= 1) ? (int)item->valuedouble : 1;
    item = cJSON_GetObjectItemCaseSensitive(root, "next_user_id");
    data->next_user_id = (cJSON_IsNumber(item) && item->valuedouble >= 1) ? (int)item->valuedouble : 1;
     printf("Loaded next IDs: Mentee=%d, Meeting=%d, Issue=%d, User=%d\n",
            data->next_mentee_id, data->next_meeting_id, data->next_issue_id, data->next_user_id); fflush(stdout);


   // --- Load Mentees ---
   cJSON* mentees_j = cJSON_GetObjectItemCaseSensitive(root, "mentees");
   int mentees_loaded_count = 0;
   int mentees_processed_count = 0; // Counter for JSON entries processed
   if (cJSON_IsArray(mentees_j)) {
       printf("    [Mentee Load] Found 'mentees' array. Processing items...\n"); fflush(stdout);
       cJSON* mi;
       cJSON_ArrayForEach(mi, mentees_j) {
           mentees_processed_count++;
           printf("    [Mentee Load] Processing JSON item #%d\n", mentees_processed_count); fflush(stdout);

           if (!cJSON_IsObject(mi)) {
               printf("    [Mentee Load] Item #%d is not a JSON object. Skipping.\n", mentees_processed_count); fflush(stdout);
               continue; // Skip if not an object
           }

           Mentee* m = malloc(sizeof(Mentee));
           if (!m) {
               fprintf(stderr, "    [Mentee Load] Warning: malloc failed for Mentee struct, skipping item #%d.\n", mentees_processed_count); fflush(stderr);
               continue;
           }

           // Extract data
           m->id = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(mi,"id"));
           m->name = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mi,"name")));
           m->subject = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mi,"subject")));
           m->email = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(mi,"email")));
           m->general_notes = json_array_to_notes(cJSON_GetObjectItemCaseSensitive(mi,"general_notes"));
           m->next = NULL;

           // Log extracted data before validation
           printf("    [Mentee Load] Extracted item #%d -> ID: %d, Name: '%s', Subject: '%s'\n",
                  mentees_processed_count, m->id, m->name ? m->name : "NULL", m->subject ? m->subject : "NULL");
           fflush(stdout);

           // Basic validation
           if (m->id == 0 || !m->name || !m->subject || (m->name && strlen(m->name)==0) || (m->subject && strlen(m->subject)==0) ) {
               fprintf(stderr, "    [Mentee Load] Warning: Skipping loaded mentee item #%d due to invalid/missing required data (ID: %d, Name: '%s', Subject: '%s').\n",
                      mentees_processed_count, m->id, m->name ? m->name : "NULL", m->subject ? m->subject : "NULL");
               fflush(stderr);
               // Free allocated memory for this invalid entry
               free(m->name); free(m->subject); free(m->email); free_notes(m->general_notes); free(m);
               continue; // Skip this invalid entry
           }

           // Add to head of list
           printf("    [Mentee Load] Validation passed for item #%d (ID: %d). Adding to list.\n", mentees_processed_count, m->id); fflush(stdout);
           m->next = data->mentees_head;
           data->mentees_head = m;
           mentees_loaded_count++;
       }
        printf("    [Mentee Load] Finished processing %d JSON items.\n", mentees_processed_count); fflush(stdout);
        printf("Loaded %d mentees from file.\n", mentees_loaded_count); fflush(stdout); // Final count
   } else {
        fprintf(stderr, "Warning: No 'mentees' array found or it's not an array in JSON data.\n"); fflush(stderr);
   }
   // --- End Load Mentees ---


    // --- Load Meetings ---
    cJSON* meetings_j = cJSON_GetObjectItemCaseSensitive(root, "meetings");
    int meetings_loaded_count = 0;
    if (cJSON_IsArray(meetings_j)) {
        cJSON* meeti;
        cJSON_ArrayForEach(meeti, meetings_j) {
            if (!cJSON_IsObject(meeti)) continue;
            Meeting* m = malloc(sizeof(Meeting));
            if (!m) { fprintf(stderr, "Warning: malloc failed for Meeting struct, skipping entry.\n"); continue; }

            m->id = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(meeti,"id"));
            m->mentee_id = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(meeti,"mentee_id"));
            // Handle potential key variations: "mentee" or "mentee_name"
            m->mentee_name = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meeti,"mentee")));
            if (!m->mentee_name) m->mentee_name = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meeti,"mentee_name"))); // Fallback

            m->date_str = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meeti,"date")));
            m->time_str = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meeti,"time")));
            m->duration_minutes = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(meeti,"duration"));
            m->notes = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(meeti,"notes")));
            m->next = NULL;

            // Basic validation
            if (m->id==0 || m->mentee_id==0 || !m->mentee_name || !m->date_str || !m->time_str || m->duration_minutes <= 0 || (m->mentee_name && strlen(m->mentee_name)==0)) {
                 fprintf(stderr, "Warning: Skipping loaded meeting with invalid/missing required data (ID: %d).\n", m->id);
                 free(m->mentee_name); free(m->date_str); free(m->time_str); free(m->notes); free(m);
                 continue;
             }
            // Add to head
            m->next = data->meetings_head;
            data->meetings_head = m;
            meetings_loaded_count++;
        }
         printf("Loaded %d meetings from file.\n", meetings_loaded_count); fflush(stdout);
    } else {
         printf("No 'meetings' array found or it's not an array in JSON data.\n"); fflush(stdout);
    }


    // --- Load Issues ---
    cJSON* issues_j = cJSON_GetObjectItemCaseSensitive(root, "issues");
     int issues_loaded_count = 0;
    if (cJSON_IsArray(issues_j)) {
        cJSON* issi;
        cJSON_ArrayForEach(issi, issues_j) {
            if (!cJSON_IsObject(issi)) continue;
            Issue* i = malloc(sizeof(Issue));
            if (!i) { fprintf(stderr, "Warning: malloc failed for Issue struct, skipping entry.\n"); continue; }

            i->id = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(issi,"id"));
            i->mentee_id = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(issi,"mentee_id"));
             // Handle potential key variations: "mentee" or "mentee_name"
            i->mentee_name = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(issi,"mentee")));
            if (!i->mentee_name) i->mentee_name = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(issi,"mentee_name"))); // Fallback

            i->description = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(issi,"description")));
            i->date_reported_str = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(issi,"date"))); // Assume "date" holds reported date
            i->priority = string_to_priority(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(issi,"priority")));
            i->status = string_to_status(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(issi,"status")));
            i->response_notes = json_array_to_notes(cJSON_GetObjectItemCaseSensitive(issi,"notes"));
            i->next = NULL;

             // Basic validation
             if (i->id==0 || i->mentee_id==0 || !i->mentee_name || !i->description || !i->date_reported_str || (i->mentee_name && strlen(i->mentee_name)==0) || (i->description && strlen(i->description)==0)) {
                 fprintf(stderr, "Warning: Skipping loaded issue with invalid/missing required data (ID: %d).\n", i->id);
                 free(i->mentee_name); free(i->description); free(i->date_reported_str); free_notes(i->response_notes); free(i);
                 continue;
             }
            // Add to head
            i->next = data->issues_head;
            data->issues_head = i;
            issues_loaded_count++;
        }
        printf("Loaded %d issues from file.\n", issues_loaded_count); fflush(stdout);
    } else {
         printf("No 'issues' array found or it's not an array in JSON data.\n"); fflush(stdout);
    }


    // --- Load Users ---
    cJSON* users_j = cJSON_GetObjectItemCaseSensitive(root, "users");
    int users_loaded_count = 0;
    if (cJSON_IsArray(users_j)) {
        cJSON* ui;
        cJSON_ArrayForEach(ui, users_j) {
            if (!cJSON_IsObject(ui)) continue;
            User* u = malloc(sizeof(User));
            if (!u) { fprintf(stderr, "Warning: malloc failed for User struct, skipping entry.\n"); continue; }

            u->id = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(ui,"id"));
            u->username = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ui,"username")));
           
            u->password = safe_strdup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ui,"password")));
            u->role = string_to_role(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(ui,"role")));
            u->associated_id = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(ui,"associated_id")); // Can be 0
            u->next = NULL;

            // Basic validation - require ID, username, password (even if insecurely stored)
            if (u->id == 0 || !u->username || !u->password || (u->username && strlen(u->username)==0) || (u->password && strlen(u->password)==0)) {
                fprintf(stderr, "Warning: Skipping loaded user with invalid/missing required data (ID: %d, Username: '%s').\n", u->id, u->username ? u->username : "NULL");
                free(u->username); free(u->password); free(u);
                continue;
            }
            // Add to head
            u->next = data->users_head;
            data->users_head = u;
            users_loaded_count++;
        }
         printf("Loaded %d users from file.\n", users_loaded_count); fflush(stdout);
    } else {
        // This is potentially problematic if the file exists but has no users
        fprintf(stderr, "Warning: No 'users' array found or it's not an array in JSON data. No users loaded.\n"); fflush(stderr);
    }

    cJSON_Delete(root); // Delete the parsed JSON structure
    printf("Data loading process complete from file %s.\n", filename); fflush(stdout);
    return data; // Return the populated AppData structure
}