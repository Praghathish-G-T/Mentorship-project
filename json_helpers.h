#ifndef JSON_HELPERS_H
#define JSON_HELPERS_H

#include <cjson/cJSON.h>
#include "mentorship_data.h" // Includes data structures

// --- JSON Serialization Prototypes (Struct -> JSON) ---
cJSON* note_to_json(const Note* note);
cJSON* note_list_to_json_array(const Note* head);
cJSON* mentee_to_json(const Mentee* mentee);
cJSON* mentee_list_to_json_array(const Mentee* head);
cJSON* meeting_to_json(const Meeting* meeting);
cJSON* meeting_list_to_json_array(const Meeting* head);
cJSON* issue_to_json(const Issue* issue);
cJSON* issue_list_to_json_array(const Issue* head);
cJSON* user_to_json(const User* user);


// --- JSON Deserialization Prototypes (JSON -> Struct) ---

/**
 * @brief Converts a cJSON array into a linked list of Note structs.
 * Allocates memory for notes and text.
 * @param json_array Pointer to the cJSON array of note objects.
 * @return Note* Pointer to the head of the new Note list or NULL.
 */
Note* json_array_to_notes(const cJSON* json_array);


#endif // JSON_HELPERS_H