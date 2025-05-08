#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include "mentorship_data.h"
#include "json_helpers.h"

// Assume safe_strdup is available (defined in mentorship_data.c)
extern char* safe_strdup(const char* s);
// Assume free_notes is available
extern void free_notes(Note* head);

// --- JSON Serialization Helpers (Struct -> JSON) ---

cJSON* note_to_json(const Note* note) {
    if (!note) return cJSON_CreateNull();
    cJSON* json = cJSON_CreateObject(); if (!json) return NULL;
    if (!cJSON_AddStringToObject(json, "text", note->text ? note->text : "") ||
        !cJSON_AddNumberToObject(json, "timestamp", (double)note->timestamp))
    { fprintf(stderr, "note_to_json: Failed add items.\n"); cJSON_Delete(json); return NULL; }
    return json;
}

cJSON* note_list_to_json_array(const Note* head) {
    cJSON* array = cJSON_CreateArray(); if (!array) return NULL;
    const Note* current = head;
    while (current) {
        cJSON* note_json = note_to_json(current);
        if (!note_json || !cJSON_AddItemToArray(array, note_json)) {
             fprintf(stderr, "note_list_to_json_array: Failed add item.\n");
             cJSON_Delete(note_json); cJSON_Delete(array); return NULL;
        }
        current = current->next;
    }
    return array;
}

cJSON* mentee_to_json(const Mentee* mentee) {
    if (!mentee) return cJSON_CreateNull();
    cJSON* json = cJSON_CreateObject(); if (!json) return NULL;
    if (!cJSON_AddNumberToObject(json, "id", mentee->id) ||
        !cJSON_AddStringToObject(json, "name", mentee->name ? mentee->name : "") ||
        !cJSON_AddStringToObject(json, "subject", mentee->subject ? mentee->subject : "") ||
        !cJSON_AddStringToObject(json, "email", mentee->email ? mentee->email : ""))
    { fprintf(stderr, "mentee_to_json: Failed add basic fields ID %d.\n", mentee->id); cJSON_Delete(json); return NULL; }

    cJSON* notes_array = note_list_to_json_array(mentee->general_notes);
    if (!notes_array) { fprintf(stderr, "mentee_to_json: Failed create notes array ID %d.\n", mentee->id); cJSON_Delete(json); return NULL; }
    if (!cJSON_AddItemToObject(json, "general_notes", notes_array)) {
         fprintf(stderr, "mentee_to_json: Failed add notes array ID %d.\n", mentee->id);
         cJSON_Delete(notes_array); cJSON_Delete(json); return NULL;
    }
    return json;
}

cJSON* mentee_list_to_json_array(const Mentee* head) {
    cJSON* array = cJSON_CreateArray(); if (!array) return NULL;
    const Mentee* current = head;
    while (current) {
         cJSON* mentee_json = mentee_to_json(current);
         if (!mentee_json || !cJSON_AddItemToArray(array, mentee_json)) {
              fprintf(stderr, "mentee_list_to_json_array: Failed add mentee %d.\n", current->id);
              cJSON_Delete(mentee_json); cJSON_Delete(array); return NULL;
         }
        current = current->next;
    }
    return array;
}

cJSON* meeting_to_json(const Meeting* meeting) {
    if (!meeting) return cJSON_CreateNull();
    cJSON* json = cJSON_CreateObject(); if(!json) return NULL;
    // Use mentee_name consistently as 'mentee' in JSON for frontend
    if (!cJSON_AddNumberToObject(json, "id", meeting->id) ||
        !cJSON_AddNumberToObject(json, "mentee_id", meeting->mentee_id) ||
        !cJSON_AddStringToObject(json, "mentee", meeting->mentee_name ? meeting->mentee_name : "") || // Changed key to 'mentee'
        !cJSON_AddStringToObject(json, "date", meeting->date_str ? meeting->date_str : "") ||
        !cJSON_AddStringToObject(json, "time", meeting->time_str ? meeting->time_str : "") ||
        !cJSON_AddNumberToObject(json, "duration", meeting->duration_minutes) ||
        !cJSON_AddStringToObject(json, "notes", meeting->notes ? meeting->notes : ""))
    { fprintf(stderr, "meeting_to_json: Failed add fields ID %d.\n", meeting->id); cJSON_Delete(json); return NULL; }
    return json;
}

cJSON* meeting_list_to_json_array(const Meeting* head) {
    cJSON* array = cJSON_CreateArray(); if (!array) return NULL;
    const Meeting* current = head;
    while (current) {
        cJSON* meeting_json = meeting_to_json(current);
         if (!meeting_json || !cJSON_AddItemToArray(array, meeting_json)) {
              fprintf(stderr, "meeting_list_to_json_array: Failed add meeting %d.\n", current->id);
              cJSON_Delete(meeting_json); cJSON_Delete(array); return NULL;
         }
        current = current->next;
    }
    return array;
}

cJSON* issue_to_json(const Issue* issue) {
     if (!issue) return cJSON_CreateNull();
     cJSON* json = cJSON_CreateObject(); if(!json) return NULL;
     // Use mentee_name consistently as 'mentee' in JSON for frontend
     if (!cJSON_AddNumberToObject(json, "id", issue->id) ||
         !cJSON_AddNumberToObject(json, "mentee_id", issue->mentee_id) ||
         !cJSON_AddStringToObject(json, "mentee", issue->mentee_name ? issue->mentee_name : "") || // Changed key to 'mentee'
         !cJSON_AddStringToObject(json, "description", issue->description ? issue->description : "") ||
         !cJSON_AddStringToObject(json, "date", issue->date_reported_str ? issue->date_reported_str : "") ||
         !cJSON_AddStringToObject(json, "priority", priority_to_string(issue->priority)) ||
         !cJSON_AddStringToObject(json, "status", status_to_string(issue->status)))
     { fprintf(stderr, "issue_to_json: Failed add basic fields ID %d.\n", issue->id); cJSON_Delete(json); return NULL; }

     cJSON* notes_array = note_list_to_json_array(issue->response_notes);
     if (!notes_array) { fprintf(stderr, "issue_to_json: Failed create notes array ID %d.\n", issue->id); cJSON_Delete(json); return NULL; }
     if (!cJSON_AddItemToObject(json, "notes", notes_array)) {
         fprintf(stderr, "issue_to_json: Failed add notes array ID %d.\n", issue->id);
         cJSON_Delete(notes_array); cJSON_Delete(json); return NULL;
     }
     return json;
}

cJSON* issue_list_to_json_array(const Issue* head) {
    cJSON* array = cJSON_CreateArray(); if (!array) return NULL;
    const Issue* current = head;
    while (current) {
         cJSON* issue_json = issue_to_json(current);
         if (!issue_json || !cJSON_AddItemToArray(array, issue_json)) {
            fprintf(stderr, "issue_list_to_json_array: Failed add issue %d.\n", current->id);
             cJSON_Delete(issue_json); cJSON_Delete(array); return NULL;
         }
        current = current->next;
    }
    return array;
}

// Moved from mentorship_data.c for better organization
// !! WARNING: Includes plain text password in JSON !!
cJSON* user_to_json(const User* user) {
     if (!user) return cJSON_CreateNull();
     cJSON* json = cJSON_CreateObject(); if (!json) return NULL;

     if (!cJSON_AddNumberToObject(json, "id", user->id) ||
         !cJSON_AddStringToObject(json, "username", user->username ? user->username : "") ||
         !cJSON_AddStringToObject(json, "password", user->password ? user->password : "") || // !! SAVING PLAIN TEXT PASSWORD !!
         !cJSON_AddStringToObject(json, "role", role_to_string(user->role)) ||
         !cJSON_AddNumberToObject(json, "associated_id", user->associated_id))
     {
          fprintf(stderr, "user_to_json: Failed to add items to user object ID %d.\n", user->id);
          cJSON_Delete(json); return NULL;
     }
     return json;
}


// --- JSON Deserialization Helpers (JSON -> Struct) ---

/**
 * @brief Converts a cJSON array into a linked list of Note structs.
 */
Note* json_array_to_notes(const cJSON* json_array) {
    if (!cJSON_IsArray(json_array)) {
        return NULL; // Expecting an array, even if empty
    }

    Note* head = NULL;
    Note* tail = NULL;
    cJSON* item;

    cJSON_ArrayForEach(item, json_array) {
        if (!cJSON_IsObject(item)) continue; // Skip non-objects

        Note* new_note = malloc(sizeof(Note));
        if (!new_note) {
            fprintf(stderr, "Malloc failed for Note struct during note list load.\n");
            free_notes(head); // Free any notes already created
            return NULL;
        }
        new_note->next = NULL;

        const cJSON* text_item = cJSON_GetObjectItemCaseSensitive(item, "text");
        const cJSON* time_item = cJSON_GetObjectItemCaseSensitive(item, "timestamp");

        const char* text_val = cJSON_GetStringValue(text_item);
        new_note->text = safe_strdup(text_val); // Handles NULL text_val
        new_note->timestamp = (time_item && cJSON_IsNumber(time_item)) ? (time_t)time_item->valuedouble : 0;

        // Check if strdup failed (but allow NULL text if original JSON was null/missing)
        if (text_val && !new_note->text) {
             fprintf(stderr, "Warning: strdup failed while loading note text. Skipping this note.\n");
             free(new_note);
             continue;
        }

        // Append to list
        if (head == NULL) {
            head = new_note;
            tail = new_note;
        } else {
            tail->next = new_note;
            tail = new_note;
        }
    }
    return head;
}