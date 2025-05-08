#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>       // For INT_MAX
#include <ctype.h>        // For tolower, isspace
#include <microhttpd.h>
#include <cjson/cJSON.h>
#include "mentorship_data.h"
#include "json_helpers.h"
#include "api_handler.h"

// ========================================================================== //
//                             DEFINITIONS                                    //
// ========================================================================== //

#define API_PREFIX "/api/"
#define LOGIN_ENDPOINT "/api/login"
#define LOGOUT_ENDPOINT "/api/logout"
#define MENTEE_API_PREFIX "/api/mentee/me/" // Prefix for mentee-specific endpoints
#define MAX_POST_SIZE 16384                 // Max size for request bodies
#define DATA_FILE "mentorship_data.json"    // Ensure consistency
#define AUTH_HEADER "X-User-ID"             // Header for user authentication token/ID

// Structure to hold state for processing POST/PATCH request bodies chunk by chunk
struct PostStatus {
    char *buffer;       // Accumulated data buffer
    size_t buffer_size; // Current size of the buffer
    int complete;       // Flag: 1 if the final chunk has been received
    int error;          // Flag: 1 if an error occurred (e.g., too large)
};

// ========================================================================== //
//                        FORWARD DECLARATIONS (STATIC)                       //
// ========================================================================== //

// --- Helper Functions ---
static enum MHD_Result send_json_response(struct MHD_Connection *connection, int status_code, cJSON *json_root);
static enum MHD_Result send_error_response(struct MHD_Connection *connection, int status_code, const char *message);
static User* authenticate_request(struct MHD_Connection *connection, AppData *app_data, UserRole required_role, int *authenticated_user_id, int *authenticated_assoc_id);
static char* generate_username_from_name(const char* full_name);

// --- Auth Handlers ---
static enum MHD_Result handle_login(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size);
static enum MHD_Result handle_logout(struct MHD_Connection *connection, AppData *app_data);

// --- Mentor API Handlers ---
static enum MHD_Result handle_get_mentees(struct MHD_Connection *connection, AppData *app_data);
static enum MHD_Result handle_post_mentees(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size);
static enum MHD_Result handle_delete_mentee(struct MHD_Connection *connection, AppData *app_data, int mentee_id);
static enum MHD_Result handle_get_meetings(struct MHD_Connection *connection, AppData *app_data);
static enum MHD_Result handle_post_meetings(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size);
static enum MHD_Result handle_patch_meeting(struct MHD_Connection *connection, AppData *app_data, int meeting_id, const char *upload_data, size_t upload_data_size);
static enum MHD_Result handle_delete_meeting(struct MHD_Connection *connection, AppData *app_data, int meeting_id);
static enum MHD_Result handle_get_issues(struct MHD_Connection *connection, AppData *app_data);
static enum MHD_Result handle_post_issues(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size); // Mentor reports issue
static enum MHD_Result handle_patch_issue(struct MHD_Connection *connection, AppData *app_data, int issue_id, const char *upload_data, size_t upload_data_size);
static enum MHD_Result handle_get_notifications(struct MHD_Connection *connection, AppData *app_data); // Mentor notifications

// --- Mentee API Handlers ---
static enum MHD_Result handle_get_mentee_details(struct MHD_Connection *connection, AppData *app_data);
static enum MHD_Result handle_get_mentee_meetings(struct MHD_Connection *connection, AppData *app_data);
static enum MHD_Result handle_get_mentee_issues(struct MHD_Connection *connection, AppData *app_data);
static enum MHD_Result handle_get_mentee_mentor(struct MHD_Connection *connection, AppData *app_data);
static enum MHD_Result handle_get_mentee_notes(struct MHD_Connection *connection, AppData *app_data);
static enum MHD_Result handle_get_mentee_notifications(struct MHD_Connection *connection, AppData *app_data); // Mentee notifications
static enum MHD_Result handle_post_mentee_issue(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size); // Mentee reports issue


// ========================================================================== //
//                         HELPER FUNCTION IMPLEMENTATIONS                    //
// ========================================================================== //

/**
 * @brief Sends a JSON response with appropriate headers. Frees json_root.
 */
static enum MHD_Result send_json_response(struct MHD_Connection *connection, int status_code, cJSON *json_root) {
    char *json_string = cJSON_PrintUnformatted(json_root);
    cJSON_Delete(json_root); // Free the cJSON object immediately
    struct MHD_Response *response = NULL;
    enum MHD_Result ret = MHD_NO;

    if (!json_string) {
        fprintf(stderr, "[API] Error: Failed to print JSON.\n");
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Internal Server Error: JSON generation failed");
    }

    response = MHD_create_response_from_buffer(strlen(json_string), json_string, MHD_RESPMEM_MUST_FREE); // MHD will free json_string

    if (response) {
        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
        // Add CORS headers - adjust origin and headers as needed for security
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type, Authorization, " AUTH_HEADER);
        MHD_add_response_header(response, "Access-Control-Expose-Headers", "Content-Type, Authorization"); // Expose headers client might need

        ret = MHD_queue_response(connection, status_code, response);
        MHD_destroy_response(response);
    } else {
        fprintf(stderr, "[API] Error: Failed to create MHD response.\n");
        free(json_string); // Need to free if MHD_create_response failed
    }
    return ret;
}

/**
 * @brief Sends a standardized error JSON response.
 */
static enum MHD_Result send_error_response(struct MHD_Connection *connection, int status_code, const char *message) {
    cJSON *error_json = cJSON_CreateObject();
    if (!error_json || !cJSON_AddStringToObject(error_json, "error", message ? message : "Unknown error")) {
        fprintf(stderr, "[API] Error: Failed creating/populating error JSON.\n");
        cJSON_Delete(error_json); // Cleanup if creation failed partially
        // Send a plain text fallback error if JSON creation fails
        const char *fallback_msg = "{\"error\":\"Internal Server Error\"}";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(fallback_msg), (void *)fallback_msg, MHD_RESPMEM_PERSISTENT);
        enum MHD_Result ret = MHD_NO;
        if(response){
            MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
            MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
            ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
            MHD_destroy_response(response);
        }
        return ret;
    }
    // Use send_json_response to handle sending and freeing the error JSON
    return send_json_response(connection, status_code, error_json);
}

/**
 * @brief Authenticates a request based on the X-User-ID header and required role.
 * @return Pointer to the authenticated User struct, or NULL if authentication fails.
 * Populates authenticated_user_id and authenticated_assoc_id if pointers are provided.
 */
static User* authenticate_request(struct MHD_Connection *connection, AppData *app_data, UserRole required_role, int *authenticated_user_id, int *authenticated_assoc_id) {
    const char *user_id_str = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, AUTH_HEADER);
    if (!user_id_str) {
        printf("[AUTH] Failed: Missing %s header.\n", AUTH_HEADER); fflush(stdout);
        return NULL;
    }

    char *endptr;
    long user_id_long = strtol(user_id_str, &endptr, 10);

    // Validate user ID format
    if (*endptr != '\0' || user_id_long <= 0 || user_id_long > INT_MAX) {
        printf("[AUTH] Failed: Invalid User ID format '%s'.\n", user_id_str); fflush(stdout);
        return NULL;
    }
    int user_id = (int)user_id_long;

    // Find user by ID
    User* current_user = app_data->users_head;
    while(current_user != NULL) {
        if (current_user->id == user_id) {
            // Check role match
            if (current_user->role == required_role) {
                 printf("[AUTH] Success: User ID %d authenticated as %s.\n", user_id, role_to_string(required_role)); fflush(stdout);
                 // Populate output parameters if provided
                 if (authenticated_user_id) *authenticated_user_id = user_id;
                 if (authenticated_assoc_id) *authenticated_assoc_id = current_user->associated_id;
                 return current_user; // Success
            } else {
                 // Role mismatch
                 printf("[AUTH] Failed: User ID %d role mismatch (Required: %s, Actual: %s).\n", user_id, role_to_string(required_role), role_to_string(current_user->role)); fflush(stdout);
                 return NULL;
            }
        }
        current_user = current_user->next;
    }

    // User ID not found
    printf("[AUTH] Failed: User ID %d not found.\n", user_id); fflush(stdout);
    return NULL;
}

/**
 * @brief Generates a simple username from a full name (lowercase, no spaces).
 * Caller must free the returned string. Returns NULL on failure.
 */
static char* generate_username_from_name(const char* full_name) {
    if (!full_name) return NULL;
    size_t len = strlen(full_name);
    char* username = malloc(len + 1); // Max possible length
    if (!username) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
        if (!isspace((unsigned char)full_name[i])) {
            username[j++] = tolower((unsigned char)full_name[i]);
        }
    }
    username[j] = '\0';

    // Optional: check if username is empty after removing spaces
    if (j == 0) {
        free(username);
        return safe_strdup("newuser"); // Fallback username
    }

    return username;
}


// ========================================================================== //
//                       LOGIN/LOGOUT HANDLERS                                //
// ========================================================================== //

/**
 * @brief Handles POST /api/login requests.
 */
static enum MHD_Result handle_login(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size) {
    printf("[API] POST /api/login\n"); fflush(stdout);
    if (!app_data) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Internal server error (app_data null)");
    if (!upload_data || upload_data_size == 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing request body");

    cJSON *root = cJSON_ParseWithLength(upload_data, upload_data_size);
    if (!root) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid JSON data");

    const cJSON *username_json = cJSON_GetObjectItemCaseSensitive(root, "username");
    const cJSON *password_json = cJSON_GetObjectItemCaseSensitive(root, "password");
    const cJSON *role_json = cJSON_GetObjectItemCaseSensitive(root, "role"); // Role selected by user in login form

    const char *username_val = cJSON_GetStringValue(username_json);
    const char *password_val = cJSON_GetStringValue(password_json);
    const char *role_str_val = cJSON_GetStringValue(role_json);

    if (!username_val || !password_val || !role_str_val) {
        cJSON_Delete(root);
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing 'username', 'password', or 'role' in JSON");
    }

    UserRole selected_role = string_to_role(role_str_val);
    // Verify credentials using the insecure plain text comparison
    User *user = verify_user_password(app_data, username_val, password_val);
    cJSON_Delete(root);

    if (user) {
        // Check if the authenticated user's role matches the role they selected on the login form
        if (user->role == selected_role) {
            printf("[AUTH] Login successful for user '%s' (ID: %d, Role: %s) - Role matched selection.\n", user->username, user->id, role_to_string(user->role)); fflush(stdout);
            // Send success response including user info
            cJSON *response_json = cJSON_CreateObject();
            if (!response_json ||
                !cJSON_AddTrueToObject(response_json, "success") ||
                !cJSON_AddStringToObject(response_json, "role", role_to_string(user->role)) ||
                !cJSON_AddNumberToObject(response_json, "userId", user->id) ||
                !cJSON_AddNumberToObject(response_json, "associatedId", user->associated_id))
            {
                cJSON_Delete(response_json);
                return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed preparing success response");
            }
            return send_json_response(connection, MHD_HTTP_OK, response_json);
        } else {
            // Password was correct, but user selected the wrong role on the form
            printf("[AUTH] Login failed for user '%s': Role mismatch (Selected: %s, Actual: %s).\n", user->username, role_to_string(selected_role), role_to_string(user->role)); fflush(stdout);
            return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Invalid credentials for the selected role");
        }
    } else {
        // Username not found or password incorrect
        printf("[AUTH] Login failed for user '%s': Invalid username or password.\n", username_val); fflush(stdout);
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Invalid username or password");
    }
}

/**
 * @brief Handles POST /api/logout requests. (Currently just acknowledges)
 */
static enum MHD_Result handle_logout(struct MHD_Connection *connection, AppData *app_data) {
     printf("[API] POST /api/logout\n"); fflush(stdout);
     (void)app_data; // Mark unused for now

     // In a real session-based system, this would invalidate the session/token.
     // Here, we just return success as the client clears its stored ID.
     cJSON *response_json = cJSON_CreateObject();
     if (!response_json || !cJSON_AddTrueToObject(response_json, "success")) {
         cJSON_Delete(response_json);
         return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Logout response creation failed");
     }
     return send_json_response(connection, MHD_HTTP_OK, response_json);
}


// ========================================================================== //
//                        MENTOR API HANDLERS                                 //
// ========================================================================== //

/** @brief GET /api/mentees */
static enum MHD_Result handle_get_mentees(struct MHD_Connection *connection, AppData *app_data) {
    printf("[API] Mentor: GET /api/mentees\n"); fflush(stdout);
    int user_id, assoc_id; // Placeholders, assoc_id not used for mentor here
    if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
    }

    cJSON *mentees_array = mentee_list_to_json_array(app_data->mentees_head);
    if (!mentees_array) {
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to serialize mentee list");
    }
    return send_json_response(connection, MHD_HTTP_OK, mentees_array);
}

/** @brief POST /api/mentees - Add Mentee AND create User account */
static enum MHD_Result handle_post_mentees(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size) {
    printf("[API] Mentor: POST /api/mentees\n"); fflush(stdout);
    int user_id, assoc_id;
    if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
    }
    if (!upload_data || upload_data_size == 0) {
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing request body");
    }

    cJSON *root = cJSON_ParseWithLength(upload_data, upload_data_size);
    if (!root) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid JSON data");

    const cJSON *name_json = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *subject_json = cJSON_GetObjectItemCaseSensitive(root, "subject");
    const cJSON *email_json = cJSON_GetObjectItemCaseSensitive(root, "email");

    const char *name_val = cJSON_GetStringValue(name_json);
    const char *subject_val = cJSON_GetStringValue(subject_json);
    const char *email_val = cJSON_GetStringValue(email_json); // Can be NULL

    if (!name_val || !subject_val || strlen(name_val) == 0 || strlen(subject_val) == 0) {
        cJSON_Delete(root);
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing or invalid 'name' or 'subject'");
    }

    // Check if mentee name already exists (optional, based on requirements)
    if (find_mentee_by_name(app_data, name_val)) {
        cJSON_Delete(root);
        return send_error_response(connection, MHD_HTTP_CONFLICT, "Mentee name already exists");
    }

    // --- Add Mentee ---
    Mentee *new_mentee = add_mentee(app_data, name_val, subject_val, email_val);
    cJSON_Delete(root); // Don't need the request JSON anymore

    if (!new_mentee) {
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to add mentee record");
    }

    // --- Create User Account for the New Mentee ---
    char* mentee_username = generate_username_from_name(new_mentee->name);
    const char* default_password = "password"; // !! INSECURE DEFAULT PASSWORD !!

    if (!mentee_username) {
         fprintf(stderr, "Warning: Failed to generate username for new mentee ID %d. User account NOT created.\n", new_mentee->id);
         // Mentee was added, but user creation failed. Proceed with success for mentee add?
         // Or maybe delete the mentee record? For now, return success for mentee add.
         if (!save_data_to_file(app_data, DATA_FILE)) { fprintf(stderr,"Error saving data after failed user generation for mentee %d\n", new_mentee->id); }
         cJSON *response_json = mentee_to_json(new_mentee);
         return send_json_response(connection, MHD_HTTP_CREATED, response_json ? response_json : cJSON_CreateObject());
    }

    User* new_user = add_user(app_data, mentee_username, default_password, ROLE_MENTEE, new_mentee->id);
    free(mentee_username); // Free the generated username string

    if (!new_user) {
        // Log warning: Mentee was created, but user account failed (e.g., username conflict after generation?)
        fprintf(stderr, "Warning: Mentee ID %d added, but failed to create associated user account (username conflict?).\n", new_mentee->id);
        // Data might have been saved by add_mentee OR add_user before failure. Explicitly save again?
         if (!save_data_to_file(app_data, DATA_FILE)) { fprintf(stderr,"Error saving data after failed user creation for mentee %d\n", new_mentee->id); }
         // Fall through to return success for the mentee creation part
    } else {
        printf("Successfully added mentee ID %d and associated user account ID %d.\n", new_mentee->id, new_user->id);
        // Data was saved inside add_user.
    }

    // --- Respond ---
    cJSON *response_json = mentee_to_json(new_mentee);
    if (!response_json) {
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to serialize newly added mentee");
    }
    return send_json_response(connection, MHD_HTTP_CREATED, response_json);
}

/** @brief DELETE /api/mentees/:id */
static enum MHD_Result handle_delete_mentee(struct MHD_Connection *connection, AppData *app_data, int mentee_id) {
    printf("[API] Mentor: DELETE /api/mentees/%d\n", mentee_id); fflush(stdout);
    int user_id, assoc_id;
    if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
    }
    if (mentee_id <= 0) {
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid mentee ID");
    }

    // TODO: Implement logic to also find and delete the associated User account
    // User* user_to_delete = find user where role=MENTEE and associated_id=mentee_id
    // delete_user(app_data, user_to_delete->id); // Need a delete_user function

    int delete_result = delete_mentee(app_data, mentee_id); // This saves data

    if (delete_result == 1) {
        // Send 204 No Content on successful deletion
        struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        if (!response) return MHD_NO; // Internal server error creating response
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type, Authorization, " AUTH_HEADER);
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NO_CONTENT, response);
        MHD_destroy_response(response);
        return ret;
    } else {
        // Mentee not found or deletion failed internally
        return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee not found or deletion failed");
    }
}

/** @brief GET /api/meetings */
static enum MHD_Result handle_get_meetings(struct MHD_Connection *connection, AppData *app_data) {
     printf("[API] Mentor: GET /api/meetings\n"); fflush(stdout);
     int user_id, assoc_id;
     if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
         return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
     }
     cJSON *meetings_array = meeting_list_to_json_array(app_data->meetings_head);
     if (!meetings_array) {
         return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to serialize meeting list");
     }
     return send_json_response(connection, MHD_HTTP_OK, meetings_array);
}

/** @brief POST /api/meetings */
static enum MHD_Result handle_post_meetings(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size) {
     printf("[API] Mentor: POST /api/meetings\n"); fflush(stdout);
     int user_id, assoc_id;
     if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
         return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
     }
     if (!upload_data || upload_data_size == 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing request body");

     cJSON* root = cJSON_ParseWithLength(upload_data,upload_data_size);
     if(!root) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid JSON");

     // Frontend sends 'mentee' field with the name
     const char* mentee_name_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"mentee"));
     const char* date_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"date"));
     const char* time_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"time"));
     const cJSON* duration_json = cJSON_GetObjectItemCaseSensitive(root,"duration");
     const char* notes_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"notes"));

     if(!mentee_name_val || !date_val || !time_val || !cJSON_IsNumber(duration_json) || duration_json->valueint <= 0){
         cJSON_Delete(root);
         return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing/invalid fields (mentee, date, time, duration)");
     }
     int duration_val = duration_json->valueint;

     // Find mentee by name to get their ID
     Mentee* mentee = find_mentee_by_name(app_data, mentee_name_val);
     if(!mentee){
         cJSON_Delete(root);
         return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Mentee not found");
     }

     Meeting* new_meeting = add_meeting(app_data, mentee->id, mentee_name_val, date_val, time_val, duration_val, notes_val); // Saves data
     cJSON_Delete(root);

     if(!new_meeting) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to add meeting");

     cJSON* response_json = meeting_to_json(new_meeting);
     if(!response_json) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,"Failed to serialize new meeting");

     return send_json_response(connection, MHD_HTTP_CREATED, response_json);
}

/** @brief PATCH /api/meetings/:id */
static enum MHD_Result handle_patch_meeting(struct MHD_Connection *connection, AppData *app_data, int meeting_id, const char *upload_data, size_t upload_data_size) {
    printf("[API] Mentor: PATCH /api/meetings/%d\n", meeting_id); fflush(stdout);
    int user_id, assoc_id;
    if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
    }
    if (meeting_id <= 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid meeting ID");
    if (!upload_data || upload_data_size == 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing request body");

    Meeting *meeting = find_meeting_by_id(app_data, meeting_id);
    if (!meeting) return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Meeting not found");

    cJSON* root = cJSON_ParseWithLength(upload_data, upload_data_size);
    if (!root) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid JSON");

    // Expecting only date and time for rescheduling
    const char* date_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "date"));
    const char* time_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "time"));

    if (!date_val || !time_val) {
        cJSON_Delete(root);
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing 'date' or 'time' for update");
    }

    int update_status = update_meeting(meeting, date_val, time_val); // Doesn't save
    cJSON_Delete(root);

    if (update_status) {
        if (!save_data_to_file(app_data, DATA_FILE)) { // Save after successful update
            fprintf(stderr, "Warning: Failed to save data after patching meeting %d\n", meeting_id); fflush(stderr);
            // Continue to respond with OK, as update in memory succeeded
        }
        cJSON* response_json = meeting_to_json(meeting);
        if (!response_json) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to serialize updated meeting");
        return send_json_response(connection, MHD_HTTP_OK, response_json);
    } else {
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to update meeting data");
    }
}

/** @brief DELETE /api/meetings/:id */
static enum MHD_Result handle_delete_meeting(struct MHD_Connection *connection, AppData *app_data, int meeting_id) {
    printf("[API] Mentor: DELETE /api/meetings/%d\n", meeting_id); fflush(stdout);
    int user_id, assoc_id;
    if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
    }
    if (meeting_id <= 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid meeting ID");

    int delete_result = delete_meeting(app_data, meeting_id); // This saves data

    if (delete_result == 1) {
        struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        if (!response) return MHD_NO;
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type, Authorization, " AUTH_HEADER);
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NO_CONTENT, response);
        MHD_destroy_response(response);
        return ret;
    } else {
        return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Meeting not found or deletion failed");
    }
}

/** @brief GET /api/issues */
static enum MHD_Result handle_get_issues(struct MHD_Connection *connection, AppData *app_data) {
     printf("[API] Mentor: GET /api/issues\n"); fflush(stdout);
     int user_id, assoc_id;
     if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
         return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
     }
     cJSON *issues_array = issue_list_to_json_array(app_data->issues_head);
     if(!issues_array) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to serialize issue list");
     return send_json_response(connection, MHD_HTTP_OK, issues_array);
}

/** @brief POST /api/issues (Mentor reports issue for a mentee) */
static enum MHD_Result handle_post_issues(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size) {
     printf("[API] Mentor: POST /api/issues\n"); fflush(stdout);
     int user_id, assoc_id;
     if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
     }
     if (!upload_data || upload_data_size == 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing body");

     cJSON* root = cJSON_ParseWithLength(upload_data,upload_data_size);
     if(!root) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid JSON");

     const char* mentee_name_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"mentee"));
     const char* description_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"description"));
     const char* priority_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"priority"));
     const char* date_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"date")); // Date reported

     if(!mentee_name_val || !description_val || !priority_val || !date_val){
         cJSON_Delete(root);
         return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing fields (mentee, description, priority, date)");
     }

     Mentee* mentee = find_mentee_by_name(app_data, mentee_name_val);
     if(!mentee){
         cJSON_Delete(root);
         return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Mentee not found");
     }

     IssuePriority prio = string_to_priority(priority_val);
     Issue* new_issue = add_issue(app_data, mentee->id, mentee_name_val, description_val, date_val, prio); // Saves data
     cJSON_Delete(root);

     if(!new_issue) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to add issue");

     cJSON* response_json = issue_to_json(new_issue);
     if(!response_json) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to serialize new issue");
     return send_json_response(connection, MHD_HTTP_CREATED, response_json);
}

/** @brief PATCH /api/issues/:id */
static enum MHD_Result handle_patch_issue(struct MHD_Connection *connection, AppData *app_data, int issue_id, const char *upload_data, size_t upload_data_size) {
    printf("[API] Mentor: PATCH /api/issues/%d\n", issue_id); fflush(stdout);
    int user_id, assoc_id;
    if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
    }
    if (issue_id <= 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid issue ID");
    if (!upload_data || upload_data_size == 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing body");

    Issue *issue = find_issue_by_id(app_data, issue_id);
    if (!issue) return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Issue not found");

    cJSON* root = cJSON_ParseWithLength(upload_data, upload_data_size);
    if (!root) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid JSON");

    const char* status_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"status"));
    const char* note_text = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"notes")); // Note text is optional

    if (!status_val) {
        cJSON_Delete(root);
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing 'status' field");
    }

    IssueStatus new_status = string_to_status(status_val);
    int update_res = update_issue_status(issue, new_status, note_text); // Doesn't save
    cJSON_Delete(root);

    if (update_res) {
        if (!save_data_to_file(app_data, DATA_FILE)) { // Save after successful update
            fprintf(stderr, "Warning: Save failed after patching issue %d\n", issue_id); fflush(stderr);
        }
        cJSON* response_json = issue_to_json(issue);
        if (!response_json) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to serialize updated issue");
        return send_json_response(connection, MHD_HTTP_OK, response_json);
    } else {
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to update issue");
    }
}

/** @brief GET /api/notifications (Mentor View) */
static enum MHD_Result handle_get_notifications(struct MHD_Connection *connection, AppData *app_data) {
     printf("[API] Mentor: GET /api/notifications\n"); fflush(stdout);
     int user_id, assoc_id;
     if (!authenticate_request(connection, app_data, ROLE_MENTOR, &user_id, &assoc_id)) {
         return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentor role required");
     }

     cJSON* notifications_array = cJSON_CreateArray();
     if(!notifications_array) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed create notification array");

     time_t now = time(NULL);
     time_t upcoming_threshold = now + (24 * 60 * 60); // Meetings within next 24 hours

     // Check upcoming meetings
     Meeting* current_meeting = app_data->meetings_head;
     while(current_meeting) {
         time_t meeting_time = 0;
         struct tm meeting_tm = {0};
         char datetime_str[20]; // "YYYY-MM-DD HH:MM" + null
         if (current_meeting->date_str && current_meeting->time_str) {
             snprintf(datetime_str, sizeof(datetime_str), "%s %s", current_meeting->date_str, current_meeting->time_str);
             // Use strptime to parse date and time
             if (strptime(datetime_str, "%Y-%m-%d %H:%M", &meeting_tm) != NULL) {
                 meeting_tm.tm_isdst = -1; // Let mktime determine DST
                 meeting_time = mktime(&meeting_tm);
             }
         }

         if (meeting_time > 0 && meeting_time > now && meeting_time < upcoming_threshold) {
             cJSON* notification_obj = cJSON_CreateObject();
             if (notification_obj) {
                 char notification_text[256];
                 snprintf(notification_text, sizeof(notification_text), "Upcoming Meeting: %s @ %s",
                          current_meeting->mentee_name ? current_meeting->mentee_name : "?",
                          current_meeting->time_str ? current_meeting->time_str : "?");
                 cJSON_AddStringToObject(notification_obj, "type", "meeting_reminder");
                 cJSON_AddStringToObject(notification_obj, "text", notification_text);
                 cJSON_AddNumberToObject(notification_obj, "timestamp", (double)meeting_time);
                 cJSON_AddNumberToObject(notification_obj, "relatedId", current_meeting->id);
                 if(!cJSON_AddItemToArray(notifications_array, notification_obj)) cJSON_Delete(notification_obj); // Add or delete if fails
             }
         }
         current_meeting = current_meeting->next;
     }

     // Check open issues (maybe newly reported ones?) - Simple approach: all open issues
     Issue* current_issue = app_data->issues_head;
     while(current_issue) {
         if(current_issue->status == STATUS_OPEN) {
             cJSON* notification_obj = cJSON_CreateObject();
             if (notification_obj) {
                 char notification_text[256];
                 char short_desc[51] = {0}; // Description snippet
                 if (current_issue->description) {
                     strncpy(short_desc, current_issue->description, 50);
                     if (strlen(current_issue->description) > 50) strcat(short_desc, "...");
                 } else {
                     strcpy(short_desc, "N/A");
                 }
                 snprintf(notification_text, sizeof(notification_text), "Open Issue (#%d): '%s' for %s",
                          current_issue->id, short_desc,
                          current_issue->mentee_name ? current_issue->mentee_name : "?");
                 cJSON_AddStringToObject(notification_obj, "type", "issue_open");
                 cJSON_AddStringToObject(notification_obj, "text", notification_text);
                 // Use report date as timestamp
                 time_t issue_time = 0;
                 struct tm issue_tm = {0};
                 if (current_issue->date_reported_str && strptime(current_issue->date_reported_str, "%Y-%m-%d", &issue_tm) != NULL) {
                      issue_tm.tm_isdst = -1; issue_time = mktime(&issue_tm);
                 }
                 cJSON_AddNumberToObject(notification_obj, "timestamp", (issue_time > 0) ? (double)issue_time : (double)now); // Fallback to now
                 cJSON_AddNumberToObject(notification_obj, "relatedId", current_issue->id);
                 if(!cJSON_AddItemToArray(notifications_array, notification_obj)) cJSON_Delete(notification_obj);
             }
         }
         current_issue = current_issue->next;
     }

     return send_json_response(connection, MHD_HTTP_OK, notifications_array);
}


// ========================================================================== //
//                        MENTEE API HANDLERS                                 //
// ========================================================================== //

/** @brief GET /api/mentee/me/details */
static enum MHD_Result handle_get_mentee_details(struct MHD_Connection *connection, AppData *app_data) {
    printf("[API] Mentee: GET /api/mentee/me/details\n"); fflush(stdout);
    int user_id, mentee_assoc_id; // Use assoc_id to find the mentee record
    User* user = authenticate_request(connection, app_data, ROLE_MENTEE, &user_id, &mentee_assoc_id);
    if (!user) {
        return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized: Mentee role required");
    }
    if (mentee_assoc_id <= 0) {
        return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee profile association missing for this user");
    }

    Mentee* mentee = find_mentee_by_id(app_data, mentee_assoc_id);
    if (!mentee) {
        return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee details not found for associated ID");
    }

    cJSON* mentee_json = mentee_to_json(mentee);
    if (!mentee_json) {
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to serialize mentee details");
    }
    return send_json_response(connection, MHD_HTTP_OK, mentee_json);
}

/** @brief GET /api/mentee/me/meetings */
static enum MHD_Result handle_get_mentee_meetings(struct MHD_Connection *connection, AppData *app_data) {
    printf("[API] Mentee: GET /api/mentee/me/meetings\n"); fflush(stdout);
    int user_id, mentee_assoc_id;
    User* user = authenticate_request(connection, app_data, ROLE_MENTEE, &user_id, &mentee_assoc_id);
    if (!user) return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized");
    if (mentee_assoc_id <= 0) return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee association missing");

    cJSON *meetings_array = cJSON_CreateArray();
    if (!meetings_array) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed create meetings array");

    Meeting* current = app_data->meetings_head;
    while (current != NULL) {
        if (current->mentee_id == mentee_assoc_id) { // Filter by the authenticated mentee's ID
            cJSON* meeting_json = meeting_to_json(current);
            // Add item or delete if add fails
            if (!meeting_json || !cJSON_AddItemToArray(meetings_array, meeting_json)) {
                cJSON_Delete(meeting_json); // Clean up the created object if adding failed
            }
        }
        current = current->next;
    }
    return send_json_response(connection, MHD_HTTP_OK, meetings_array);
}

/** @brief GET /api/mentee/me/issues */
static enum MHD_Result handle_get_mentee_issues(struct MHD_Connection *connection, AppData *app_data) {
    printf("[API] Mentee: GET /api/mentee/me/issues\n"); fflush(stdout);
    int user_id, mentee_assoc_id;
    User* user = authenticate_request(connection, app_data, ROLE_MENTEE, &user_id, &mentee_assoc_id);
    if (!user) return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized");
    if (mentee_assoc_id <= 0) return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee association missing");

    cJSON *issues_array = cJSON_CreateArray();
    if (!issues_array) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed create issues array");

    Issue* current = app_data->issues_head;
    while (current != NULL) {
        if (current->mentee_id == mentee_assoc_id) { // Filter by mentee ID
            cJSON* issue_json = issue_to_json(current);
            if (!issue_json || !cJSON_AddItemToArray(issues_array, issue_json)) {
                cJSON_Delete(issue_json);
            }
        }
        current = current->next;
    }
    return send_json_response(connection, MHD_HTTP_OK, issues_array);
}

/** @brief GET /api/mentee/me/mentor */
static enum MHD_Result handle_get_mentee_mentor(struct MHD_Connection *connection, AppData *app_data) {
     printf("[API] Mentee: GET /api/mentee/me/mentor\n"); fflush(stdout);
     int user_id, mentee_assoc_id;
     User* user = authenticate_request(connection, app_data, ROLE_MENTEE, &user_id, &mentee_assoc_id);
     if (!user) return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized");

     // Find the first user with ROLE_MENTOR (assuming single mentor system)
     // In a multi-mentor system, this would need linking info.
     User* mentor_user = NULL;
     User* current_user = app_data->users_head;
     while (current_user) {
         if (current_user->role == ROLE_MENTOR) {
             mentor_user = current_user;
             break;
         }
         current_user = current_user->next;
     }

     if (!mentor_user) {
         return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentor details not found in the system");
     }

     // Create a simplified JSON object for mentor details
     // Avoid sending password hash or internal details
     cJSON *mentor_json = cJSON_CreateObject();
     if (!mentor_json ||
         !cJSON_AddNumberToObject(mentor_json, "id", mentor_user->id) || // Mentor's user ID
         !cJSON_AddStringToObject(mentor_json, "name", mentor_user->username ? mentor_user->username : "Mentor") || // Use username as name
         // Add placeholder/dummy details if needed by frontend
         !cJSON_AddStringToObject(mentor_json, "email", "mentor@example.com") || // Placeholder email
         !cJSON_AddStringToObject(mentor_json, "subject", "Mentorship Program")) // Placeholder subject
     {
          cJSON_Delete(mentor_json);
          return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed create mentor JSON");
     }
     return send_json_response(connection, MHD_HTTP_OK, mentor_json);
}

/** @brief GET /api/mentee/me/notes */
static enum MHD_Result handle_get_mentee_notes(struct MHD_Connection *connection, AppData *app_data) {
    printf("[API] Mentee: GET /api/mentee/me/notes\n"); fflush(stdout);
    int user_id, mentee_assoc_id;
    User* user = authenticate_request(connection, app_data, ROLE_MENTEE, &user_id, &mentee_assoc_id);
    if (!user) return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized");
    if (mentee_assoc_id <= 0) return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee association missing");

    Mentee* mentee = find_mentee_by_id(app_data, mentee_assoc_id);
    if (!mentee) return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee details not found");

    // Return the mentee's general notes
    cJSON* notes_array = note_list_to_json_array(mentee->general_notes);
    if (!notes_array) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed serialize general notes");
    return send_json_response(connection, MHD_HTTP_OK, notes_array);
}

/** @brief GET /api/mentee/me/notifications */
static enum MHD_Result handle_get_mentee_notifications(struct MHD_Connection *connection, AppData *app_data) {
     printf("[API] Mentee: GET /api/mentee/me/notifications\n"); fflush(stdout);
     int user_id, mentee_assoc_id;
     User* user = authenticate_request(connection, app_data, ROLE_MENTEE, &user_id, &mentee_assoc_id);
     if (!user) return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized");
     if (mentee_assoc_id <= 0) return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee association missing");

     cJSON *notifications_array = cJSON_CreateArray();
     if (!notifications_array) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed create notification array");

     time_t now = time(NULL);
     time_t upcoming_meeting_thresh = now + (24 * 60 * 60); // Meetings within next 24 hours
     time_t recent_update_thresh = now - (24 * 60 * 60); // Issue updates within last 24 hours

     // Check upcoming meetings for this mentee
     Meeting* current_meeting = app_data->meetings_head;
     while (current_meeting) {
         if (current_meeting->mentee_id == mentee_assoc_id) {
             time_t meeting_time = 0; struct tm meeting_tm = {0}; char dtstr[20];
             if (current_meeting->date_str && current_meeting->time_str) {
                 snprintf(dtstr, sizeof(dtstr), "%s %s", current_meeting->date_str, current_meeting->time_str);
                 if (strptime(dtstr, "%Y-%m-%d %H:%M", &meeting_tm) != NULL) {
                     meeting_tm.tm_isdst = -1; meeting_time = mktime(&meeting_tm);
                 }
             }
             if (meeting_time > 0 && meeting_time > now && meeting_time < upcoming_meeting_thresh) {
                 cJSON* no = cJSON_CreateObject(); if (no) {
                     char nt[256];
                     snprintf(nt, sizeof(nt), "Upcoming meeting with mentor on %s at %s",
                              current_meeting->date_str?current_meeting->date_str:"?",
                              current_meeting->time_str?current_meeting->time_str:"?");
                     cJSON_AddStringToObject(no,"type","meeting_reminder");
                     cJSON_AddStringToObject(no,"text",nt);
                     cJSON_AddNumberToObject(no,"timestamp",(double)meeting_time);
                     cJSON_AddNumberToObject(no,"relatedId",current_meeting->id);
                     if(!cJSON_AddItemToArray(notifications_array,no)) cJSON_Delete(no);
                 }
             }
         }
         current_meeting = current_meeting->next;
     }

     // Check recent issue updates for this mentee
     Issue* current_issue = app_data->issues_head;
     while (current_issue) {
         if (current_issue->mentee_id == mentee_assoc_id) {
             // Check if status is In Progress or Resolved, and if there are notes
             if (current_issue->status == STATUS_IN_PROGRESS || current_issue->status == STATUS_RESOLVED) {
                 // Find the timestamp of the *last* note added
                 Note* last_note = current_issue->response_notes; // Assuming notes are added to head
                 time_t update_time = 0;
                 if (last_note) {
                     update_time = last_note->timestamp;
                 } else {
                     // If no notes, use report date as a fallback (less ideal)
                     struct tm issue_tm={0};
                     if(current_issue->date_reported_str && strptime(current_issue->date_reported_str,"%Y-%m-%d",&issue_tm)!=NULL){
                         issue_tm.tm_isdst=-1; update_time = mktime(&issue_tm);
                     }
                 }

                 // Check if update is recent
                 if (update_time > 0 && update_time > recent_update_thresh) {
                     cJSON* no = cJSON_CreateObject(); if (no) {
                         char nt[256];
                         char short_desc[51]={0};
                         if(current_issue->description){ strncpy(short_desc,current_issue->description,50); if(strlen(current_issue->description)>50) strcat(short_desc,"..."); } else strcpy(short_desc,"N/A");
                         snprintf(nt,sizeof(nt),"Issue #%d ('%s') status updated to: %s",
                                  current_issue->id, short_desc, status_to_string(current_issue->status));
                         cJSON_AddStringToObject(no,"type","issue_update");
                         cJSON_AddStringToObject(no,"text",nt);
                         cJSON_AddNumberToObject(no,"timestamp",(double)update_time);
                         cJSON_AddNumberToObject(no,"relatedId",current_issue->id);
                         if(!cJSON_AddItemToArray(notifications_array,no)) cJSON_Delete(no);
                     }
                 }
             }
         }
         current_issue = current_issue->next;
     }
     return send_json_response(connection, MHD_HTTP_OK, notifications_array);
}

/** @brief POST /api/mentee/me/issues (Mentee reports issue) */
static enum MHD_Result handle_post_mentee_issue(struct MHD_Connection *connection, AppData *app_data, const char *upload_data, size_t upload_data_size) {
    printf("[API] Mentee: POST /api/mentee/me/issues\n"); fflush(stdout);
    int user_id, mentee_assoc_id;
    User* user = authenticate_request(connection, app_data, ROLE_MENTEE, &user_id, &mentee_assoc_id);
    if (!user) return send_error_response(connection, MHD_HTTP_UNAUTHORIZED, "Unauthorized");
    if (mentee_assoc_id <= 0) return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee association missing");
    if (!upload_data || upload_data_size == 0) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing request body");

    // Find the mentee record to get their name
    Mentee* mentee = find_mentee_by_id(app_data, mentee_assoc_id);
    if (!mentee) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Could not find mentee profile for authenticated user");

    cJSON* root = cJSON_ParseWithLength(upload_data,upload_data_size);
    if (!root) return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid JSON");

    const char* description_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"description"));
    const char* priority_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"priority"));
    const char* date_val = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root,"date")); // Date reported

    if(!description_val || !priority_val || !date_val || !strlen(description_val)){
        cJSON_Delete(root);
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing/invalid fields (description, priority, date)");
    }

    IssuePriority prio = string_to_priority(priority_val);
    // Add the issue using the authenticated mentee's ID and name
    Issue* new_issue = add_issue(app_data, mentee->id, mentee->name, description_val, date_val, prio); // Saves data
    cJSON_Delete(root);

    if(!new_issue) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to report issue");

    cJSON* response_json = issue_to_json(new_issue);
    if(!response_json) return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,"Failed to serialize reported issue");
    return send_json_response(connection, MHD_HTTP_CREATED, response_json);
}


// ========================================================================== //
//                         MAIN REQUEST HANDLER (ROUTER)                      //
// ========================================================================== //

enum MHD_Result request_handler(void *cls, struct MHD_Connection *connection,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {

    (void)version; // Mark unused

    printf("[ROUTER] %s %s\n", method, url); fflush(stdout);
    AppData *app_data = (AppData *)cls;
    enum MHD_Result ret = MHD_NO; // Default to error/unhandled

    // --- CORS Preflight Handling ---
     if (0 == strcmp(method, MHD_HTTP_METHOD_OPTIONS)) {
         printf("[ROUTER] Handling OPTIONS request for CORS preflight.\n"); fflush(stdout);
         struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
         if (!response) return MHD_NO;
         MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
         MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
         MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type, Authorization, " AUTH_HEADER);
         MHD_add_response_header(response, "Access-Control-Max-Age", "86400"); // Cache preflight for 1 day
         ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
         MHD_destroy_response(response);
         return ret;
     }

    // --- POST/PATCH Data Accumulation ---
    struct PostStatus *post_status = NULL;
    if (*con_cls == NULL) {
        // First call for this connection
        if (0 == strcmp(method, MHD_HTTP_METHOD_POST) || 0 == strcmp(method, MHD_HTTP_METHOD_PATCH)) {
            // Expecting body data, allocate status struct
            post_status = malloc(sizeof(struct PostStatus));
            if (!post_status) { fprintf(stderr,"[ROUTER] Failed malloc PostStatus\n"); return MHD_NO; }
            post_status->buffer = NULL;
            post_status->buffer_size = 0;
            post_status->complete = 0;
            post_status->error = 0;
            *con_cls = (void *)post_status;
            printf("[ROUTER] Initialized PostStatus for %s.\n", method); fflush(stdout);
            return MHD_YES; // Ask for more data
        } else {
            // Not POST/PATCH, mark connection context as processed (simple flag)
            *con_cls = (void*)1; // Use 1 to indicate no PostStatus needed/allocated
             printf("[ROUTER] No PostStatus needed for %s.\n", method); fflush(stdout);
        }
    }

    if (*con_cls != (void*)1) {
        // If context is not the simple flag (1), it must be a PostStatus struct
        post_status = *con_cls;
         if (!post_status) { fprintf(stderr,"[ROUTER] Invalid PostStatus state\n"); return MHD_NO; } // Should not happen

        if (post_status->error) {
            printf("[ROUTER] PostStatus error flag set, ignoring data.\n"); fflush(stdout);
             *upload_data_size = 0; // Discard any further data
             if (*upload_data_size == 0) post_status->complete = 1; // Mark as complete on final call even if error
             return MHD_YES;
        }

        if (*upload_data_size > 0) {
            // Accumulate data
            size_t new_size = post_status->buffer_size + *upload_data_size;
            if (new_size > MAX_POST_SIZE) {
                fprintf(stderr, "[ROUTER] Error: POST data exceeds MAX_POST_SIZE (%zu > %d).\n", new_size, MAX_POST_SIZE);
                free(post_status->buffer); // Free existing buffer
                post_status->buffer = NULL;
                post_status->buffer_size = 0;
                post_status->error = 1;    // Set error flag
                 *upload_data_size = 0;     // Discard current chunk
                return MHD_YES; // Still need to wait for the final call
            }
            // Reallocate and copy
            char *new_buffer = realloc(post_status->buffer, new_size);
            if (!new_buffer) {
                fprintf(stderr, "[ROUTER] Error: Failed realloc for POST buffer.\n");
                free(post_status->buffer);
                post_status->buffer = NULL; post_status->buffer_size = 0; post_status->error = 1; *upload_data_size = 0;
                return MHD_YES; // Wait for final call
            }
            post_status->buffer = new_buffer;
            memcpy(post_status->buffer + post_status->buffer_size, upload_data, *upload_data_size);
            post_status->buffer_size = new_size;
            *upload_data_size = 0; // Indicate data was processed
             printf("[ROUTER] Accumulated %zu bytes for POST/PATCH.\n", post_status->buffer_size); fflush(stdout);
            return MHD_YES; // Ask for more data
        } else {
            // Final call (*upload_data_size == 0)
            post_status->complete = 1;
            printf("[ROUTER] Final POST/PATCH chunk received. Total size: %zu\n", post_status->buffer_size); fflush(stdout);
        }
    }

    // --- Routing Logic ---
    // At this point, for POST/PATCH, post_status->complete should be 1 if all data is ready
    // For GET/DELETE, *con_cls is (void*)1

    // Check if body processing is needed but not complete/ready
     if ((0 == strcmp(method, MHD_HTTP_METHOD_POST) || 0 == strcmp(method, MHD_HTTP_METHOD_PATCH)) && (!post_status || !post_status->complete)) {
         // This case might occur if the first call somehow skipped initialization,
         // or if the final call hasn't happened yet.
         // We already returned MHD_YES above if accumulation is ongoing.
         // If we reach here unexpectedly, treat as error.
         fprintf(stderr, "[ROUTER] Error: POST/PATCH request reached routing logic prematurely.\n");
         ret = send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Request processing error");
         goto cleanup; // Jump to cleanup
     }

    // --- Authentication Endpoints ---
    if (0 == strcmp(url, LOGIN_ENDPOINT) && 0 == strcmp(method, MHD_HTTP_METHOD_POST)) {
        if(post_status && post_status->error) ret = send_error_response(connection, MHD_HTTP_CONTENT_TOO_LARGE, "Request body too large");
        else ret = handle_login(connection, app_data, post_status ? post_status->buffer : NULL, post_status ? post_status->buffer_size : 0);
    } else if (0 == strcmp(url, LOGOUT_ENDPOINT) && 0 == strcmp(method, MHD_HTTP_METHOD_POST)) {
        // Logout doesn't strictly need the body, but we wait for completion anyway.
         ret = handle_logout(connection, app_data); // Pass app_data if needed later
    }
    // --- Mentee Endpoints ---
    else if (0 == strncmp(url, MENTEE_API_PREFIX, strlen(MENTEE_API_PREFIX))) {
        const char *endpoint = url + strlen(MENTEE_API_PREFIX);
        if (0 == strcmp(method, MHD_HTTP_METHOD_GET)) {
            if (0 == strcmp(endpoint, "details")) ret = handle_get_mentee_details(connection, app_data);
            else if (0 == strcmp(endpoint, "meetings")) ret = handle_get_mentee_meetings(connection, app_data);
            else if (0 == strcmp(endpoint, "issues")) ret = handle_get_mentee_issues(connection, app_data);
            else if (0 == strcmp(endpoint, "mentor")) ret = handle_get_mentee_mentor(connection, app_data);
            else if (0 == strcmp(endpoint, "notes")) ret = handle_get_mentee_notes(connection, app_data);
            else if (0 == strcmp(endpoint, "notifications")) ret = handle_get_mentee_notifications(connection, app_data);
            else ret = send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee API endpoint not found");
        } else if (0 == strcmp(method, MHD_HTTP_METHOD_POST)) {
            if (0 == strcmp(endpoint, "issues")) { // Mentee reporting an issue
                 if(post_status && post_status->error) ret = send_error_response(connection, MHD_HTTP_CONTENT_TOO_LARGE, "Request body too large");
                 else ret = handle_post_mentee_issue(connection, app_data, post_status ? post_status->buffer : NULL, post_status ? post_status->buffer_size : 0);
             } else {
                 ret = send_error_response(connection, MHD_HTTP_NOT_FOUND, "Mentee POST endpoint not found");
             }
        } else {
            ret = send_error_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, "Method Not Allowed on mentee endpoint");
        }
    }
    // --- Mentor Endpoints (Generic API Prefix) ---
    else if (0 == strncmp(url, API_PREFIX, strlen(API_PREFIX))) {
        const char *endpoint = url + strlen(API_PREFIX);
        if (0 == strcmp(method, MHD_HTTP_METHOD_GET)) {
            if (0 == strcmp(endpoint, "mentees")) ret = handle_get_mentees(connection, app_data);
            else if (0 == strcmp(endpoint, "meetings")) ret = handle_get_meetings(connection, app_data);
            else if (0 == strcmp(endpoint, "issues")) ret = handle_get_issues(connection, app_data);
            else if (0 == strcmp(endpoint, "notifications")) ret = handle_get_notifications(connection, app_data);
            else ret = send_error_response(connection, MHD_HTTP_NOT_FOUND, "API GET endpoint not found");
        } else if (0 == strcmp(method, MHD_HTTP_METHOD_POST)) {
             if(post_status && post_status->error) ret = send_error_response(connection, MHD_HTTP_CONTENT_TOO_LARGE, "Request body too large");
             else if (0 == strcmp(endpoint, "mentees")) ret = handle_post_mentees(connection, app_data, post_status->buffer, post_status->buffer_size);
             else if (0 == strcmp(endpoint, "meetings")) ret = handle_post_meetings(connection, app_data, post_status->buffer, post_status->buffer_size);
             else if (0 == strcmp(endpoint, "issues")) ret = handle_post_issues(connection, app_data, post_status->buffer, post_status->buffer_size); // Mentor reports issue
             else ret = send_error_response(connection, MHD_HTTP_NOT_FOUND, "API POST endpoint not found");
        } else if (0 == strcmp(method, MHD_HTTP_METHOD_PATCH)) {
             if(post_status && post_status->error) ret = send_error_response(connection, MHD_HTTP_CONTENT_TOO_LARGE, "Request body too large");
             else if (0 == strncmp(endpoint, "meetings/", 9)) { // e.g., /api/meetings/123
                 int id = atoi(endpoint + 9);
                 if(id > 0) ret = handle_patch_meeting(connection, app_data, id, post_status->buffer, post_status->buffer_size);
                 else ret = send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid meeting ID format");
             } else if (0 == strncmp(endpoint, "issues/", 7)) { // e.g., /api/issues/45
                 int id = atoi(endpoint + 7);
                 if(id > 0) ret = handle_patch_issue(connection, app_data, id, post_status->buffer, post_status->buffer_size);
                 else ret = send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid issue ID format");
             } else {
                 ret = send_error_response(connection, MHD_HTTP_NOT_FOUND, "API PATCH endpoint not found");
             }
        } else if (0 == strcmp(method, MHD_HTTP_METHOD_DELETE)) {
             if (0 == strncmp(endpoint, "mentees/", 8)) { // e.g., /api/mentees/123
                 int id = atoi(endpoint + 8);
                 if(id > 0) ret = handle_delete_mentee(connection, app_data, id);
                 else ret = send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid mentee ID format");
             } else if (0 == strncmp(endpoint, "meetings/", 9)) { // e.g., /api/meetings/123
                 int id = atoi(endpoint + 9);
                 if(id > 0) ret = handle_delete_meeting(connection, app_data, id);
                 else ret = send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid meeting ID format");
             } else {
                 ret = send_error_response(connection, MHD_HTTP_NOT_FOUND, "API DELETE endpoint not found");
             }
        } else {
            ret = send_error_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, "Method Not Allowed on this API path");
        }
    }
    // --- Fallback for unknown URLs ---
    else {
        ret = send_error_response(connection, MHD_HTTP_NOT_FOUND, "Endpoint not found");
    }

cleanup:
    // --- Cleanup PostStatus if it was used ---
    if (post_status != NULL && post_status->complete) {
         printf("[ROUTER] Cleaning up PostStatus.\n"); fflush(stdout);
        free(post_status->buffer); // Free accumulated data
        free(post_status);         // Free the status struct itself
        *con_cls = NULL;           // Clear connection context
    } else if (*con_cls == (void*)1) {
        *con_cls = NULL; // Clear the simple flag if it was set
    }

    printf("[ROUTER] Request handling finished for %s %s, MHD result: %d.\n", method, url, ret); fflush(stdout);
    return ret;
}