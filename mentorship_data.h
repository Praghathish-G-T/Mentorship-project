#ifndef MENTORSHIP_DATA_H
#define MENTORSHIP_DATA_H

#include <time.h>
#include <cjson/cJSON.h>

// --- Forward Declarations ---
typedef struct Mentee Mentee;
typedef struct Meeting Meeting;
typedef struct Issue Issue;
typedef struct Note Note;
typedef struct User User;

// --- Enums ---
typedef enum {
    PRIORITY_LOW, PRIORITY_MEDIUM, PRIORITY_HIGH
} IssuePriority;

typedef enum {
    STATUS_OPEN, STATUS_IN_PROGRESS, STATUS_RESOLVED
} IssueStatus;

typedef enum {
    ROLE_MENTOR, ROLE_MENTEE
} UserRole;

// --- Basic Structures ---

// Note structure
struct Note {
    char* text;       // Dynamically allocated note content
    time_t timestamp;
    Note* next;       // Linked list pointer
};

// Meeting structure
struct Meeting {
    int id;
    int mentee_id;
    char* mentee_name;    // Dynamically allocated
    char* date_str;       // "YYYY-MM-DD" (Dynamically allocated)
    char* time_str;       // "HH:MM" (Dynamically allocated)
    int duration_minutes;
    char* notes;          // Dynamically allocated
    Meeting* next;        // Linked list pointer
};

// Issue structure
struct Issue {
    int id;
    int mentee_id;
    char* mentee_name;       // Dynamically allocated
    char* description;       // Dynamically allocated
    char* date_reported_str; // "YYYY-MM-DD" (Dynamically allocated)
    IssuePriority priority;
    IssueStatus status;
    Note* response_notes;    // Linked list of notes
    Issue* next;             // Linked list pointer
};

// Mentee structure
struct Mentee {
    int id;
    char* name;          // Dynamically allocated
    char* subject;       // Dynamically allocated
    char* email;         // Dynamically allocated (optional)
    Note* general_notes; // Linked list of general notes
    Mentee* next;        // Linked list pointer
};

// User structure
struct User {
    int id;
    char* username;      // Dynamically allocated
    char* password;      // Dynamically allocated - !! WARNING: PLAIN TEXT !!
    UserRole role;
    int associated_id;   // Corresponding Mentee/Mentor ID (0 if admin/none)
    User* next;          // Linked list pointer
};

// --- Main Data Container ---
typedef struct {
    Mentee* mentees_head;
    Meeting* meetings_head;
    Issue* issues_head;
    User* users_head;
    int next_mentee_id;
    int next_meeting_id;
    int next_issue_id;
    int next_user_id;
} AppData;


// --- Function Prototypes ---

// Initialization / Cleanup
AppData* initialize_app_data();
void free_app_data(AppData* data);

// Persistence (Saving/Loading data to/from JSON)
int save_data_to_file(const AppData* data, const char* filename);
AppData* load_data_from_file(const char* filename);

// Mentee Functions
Mentee* add_mentee(AppData* data, const char* name, const char* subject, const char* email);
Mentee* find_mentee_by_id(const AppData* data, int id);
Mentee* find_mentee_by_name(const AppData* data, const char* name);
int delete_mentee(AppData* data, int id); // TODO: Needs to delete associated User
void add_mentee_note(Mentee* mentee, const char* note_text);
void free_mentees(Mentee* head); // Added prototype

// Meeting Functions
Meeting* add_meeting(AppData* data, int mentee_id, const char* mentee_name, const char* date_str, const char* time_str, int duration, const char* notes);
Meeting* find_meeting_by_id(const AppData* data, int id);
int update_meeting(Meeting* meeting, const char* new_date_str, const char* new_time_str);
int delete_meeting(AppData* data, int meeting_id);
void free_meetings(Meeting* head); // Added prototype

// Issue Functions
Issue* add_issue(AppData* data, int mentee_id, const char* mentee_name, const char* description, const char* date_reported, IssuePriority priority);
Issue* find_issue_by_id(const AppData* data, int id);
int update_issue_status(Issue* issue, IssueStatus new_status, const char* note_text);
void free_issues(Issue* head); // Added prototype


// User Functions
User* add_user(AppData* data, const char* username, const char* password, UserRole role, int associated_id);
User* find_user_by_username(const AppData* data, const char* username);
User* verify_user_password(const AppData* data, const char* username, const char* password);
void free_users(User* head);

// Note Functions
Note* add_note(Note** head_ref, const char* text);
void free_notes(Note* head);

// Utility Functions
char* safe_strdup(const char* s);

// String <-> Enum Conversion Helpers
const char* priority_to_string(IssuePriority p);
IssuePriority string_to_priority(const char* s);
const char* status_to_string(IssueStatus s);
IssueStatus string_to_status(const char* s);
const char* role_to_string(UserRole r);
UserRole string_to_role(const char* s);


#endif // MENTORSHIP_DATA_H