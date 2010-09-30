#include <string.h>
#include <assert.h>
#include <glib.h>
#include "tag_utility.h"


/* internal taglib structure */
struct tag_entry{
    int m_line_type;
    char * m_line_tag;
    int m_num_of_values;
    char ** m_required_tags;
    /* char ** m_optional_tags; */
    /* int m_optional_count = 0; */
    char ** m_ignored_tags;
};

tag_entry tag_entry_copy(int line_type, const char * line_tag,
                         int num_of_values,
                         char * required_tags[],
                         char * ignored_tags[]){
    tag_entry entry;
    entry.m_line_type = line_type;
    entry.m_line_tag = g_strdup( line_tag );
    entry.m_num_of_values = num_of_values;
    entry.m_required_tags = g_strdupv( required_tags );
    entry.m_ignored_tags = g_strdupv( ignored_tags );
    return entry;
}

tag_entry tag_entry_clone(tag_entry * entry){
    return tag_entry_copy(entry->m_line_type, entry->m_line_tag,
                          entry->m_num_of_values,
                          entry->m_required_tags, entry->m_ignored_tags);
}

void tag_entry_reclaim(tag_entry * entry){
    g_free( entry->m_line_tag );
    g_strfreev( entry->m_required_tags );
    g_strfreev(entry->m_ignored_tags);
}

static bool taglib_free_tag_array(GArray * tag_array){
    for ( size_t i = 0; i < tag_array->len; ++i) {
        tag_entry * entry = &g_array_index(tag_array, tag_entry, i);
        tag_entry_reclaim(entry);
    }
    g_array_free(tag_array, TRUE);
    return true;
}

/* Pointer Array of Array of tag_entry */
static GPtrArray * g_tagutils_stack = NULL;

bool taglib_init(){
    assert( g_tagutils_stack == NULL);
    g_tagutils_stack = g_ptr_array_new();
    GArray * tag_array = g_array_new(TRUE, TRUE, sizeof(tag_entry));
    g_ptr_array_add(g_tagutils_stack, tag_array);
    return true;
}

bool taglib_add_tag(int line_type, const char * line_tag, int num_of_values,
                    const char * required_tags[], const char * ignored_tags[]){
    GArray * tag_array = (GArray *) g_ptr_array_index(g_tagutils_stack,
                                     g_tagutils_stack->len - 1);

    /* some duplicate tagname or line_type check here. */
    for ( size_t i = 0; i < tag_array->len; ++i) {
        tag_entry * entry = &g_array_index(tag_array, tag_entry, i);
        if ( entry->m_line_type == line_type ||
             strcmp( entry->m_line_tag, line_tag ) == 0 )
            return false;
    }

    tag_entry entry = tag_entry_copy(line_type, line_tag, num_of_values,
                                     (char **)required_tags,
                                     (char **)ignored_tags);
    g_array_append_val(tag_array, entry);
    return true;
}

static void ptr_array_entry_free(gpointer data, gpointer user_data){
    g_free(data);
}

static gboolean hash_table_key_value_free(gpointer key, gpointer value,
                                          gpointer user_data){
    g_free(key);
    g_free(value);
    return TRUE;
}

bool taglib_read(const char * input_line, int & line_type, GPtrArray * values,
                 GHashTable * required){
    /* reset values and required. */
    g_ptr_array_foreach(values, ptr_array_entry_free, NULL);
    g_ptr_array_set_size(values, 0);
    g_hash_table_foreach_steal(required, hash_table_key_value_free, NULL);

    /* use own version of string split
       instead of g_strsplit_set for special token.*/
    char ** tokens = g_strsplit_set(input_line, " \t", -1);
    int num_of_tokens = g_strv_length(tokens);

    char * line_tag = tokens[0];
    GArray * tag_array = (GArray *) g_ptr_array_index(g_tagutils_stack, g_tagutils_stack->len - 1);

    tag_entry * cur_entry = NULL;
    /* find line type. */
    for ( size_t i = 0; i < tag_array->len; ++i) {
        tag_entry * entry = &g_array_index(tag_array, tag_entry, i);
        if ( strcmp( entry->m_line_tag, line_tag ) == 0 ) {
            cur_entry = entry;
            break;
        }
    }

    if ( !cur_entry )
        return false;

    line_type = cur_entry->m_line_type;

    for ( int i = 1; i < cur_entry->m_num_of_values + 1; ++i) {
        g_return_val_if_fail(i < num_of_tokens, false);
        char * value = g_strdup( tokens[i] );
        g_ptr_array_add(values, value);
    }

    for ( int i = cur_entry->m_num_of_values + 1; i < num_of_tokens; ++i){
        g_return_val_if_fail(i < num_of_tokens, false);
        const char * tmp = tokens[i];

        /* check ignored tags. */
        bool ignored = false;
        int ignored_len = g_strv_length( entry->m_ignored_tags );
        for ( int m = 0; m < ignored_len; ++m) {
            if ( strcmp(tmp, entry->m_ignored_tags[i]) == 0) {
                ignored = true;
                break;
            }
        }

        if ( ignored ) {
            ++i;
            continue;
        }

        /* check required tags. */
        bool required = false;
        int required_len = g_strv_length( entry->m_required_tags);
        for ( int m = 0; m < required_len; ++m) {
            if ( strcmp(tmp, entry->m_required_tags[i]) == 0) {
                required = true;
                break;
            }
        }

        /* warning on the un-expected tags. */
        if ( !required ) {
            g_warning("un-expected tags:%s.\n", tmp);
            ++i;
            continue;
        }

        char * key = g_strdup(tokens[i]);
        ++i;
        g_return_val_if_fail(i < num_of_tokens, false);
        char * value = g_strdup(tokens[i]);
        g_hash_table_insert(required, key, value);
    }

    g_strfreev(tokens);
    return true;
}

bool taglib_remove_tag(int line_type){
    /* Note: duplicate entry check is in taglib_add_tag. */
    GArray * tag_array = (GArray *) g_ptr_array_index(g_tagutils_stack, g_tagutils_stack->len - 1);
    for ( size_t i = 0; i < tag_array->len; ++i) {
        tag_entry * entry = &g_array_index(tag_array, tag_entry, i);
        if (entry->m_line_type != line_type)
            continue;
        tag_entry_reclaim(entry);
        g_array_remove_index(tag_array, i);
        return true;
    }
    return false;
}

bool taglib_push_state(){
    assert(g_tagutils_stack->len >= 1);
    GArray * next_tag_array = g_array_new(TRUE, TRUE, sizeof(tag_entry));
    GArray * prev_tag_array = (GArray *) g_ptr_array_index(g_tagutils_stack, g_tagutils_stack->len - 1);
    for ( size_t i = 0; i < prev_tag_array->len; ++i) {
        tag_entry * entry = &g_array_index(prev_tag_array, tag_entry, i);
        tag_entry new_entry = tag_entry_clone(entry);
        g_array_append_val(next_tag_array, new_entry);
    }
    g_ptr_array_add(g_tagutils_stack, next_tag_array);
    return true;
}

bool taglib_pop_state(){
    assert(g_tagutils_stack->len > 1);
    GArray * tag_array = (GArray *) g_ptr_array_index(g_tagutils_stack, g_tagutils_stack->len - 1);
    g_ptr_array_remove_index(g_tagutils_stack, g_tagutils_stack->len - 1);
    taglib_free_tag_array(tag_array);
    return true;
}

bool taglib_fini(){
    for ( size_t i = 0; i < g_tagutils_stack->len; ++i){
        GArray * tag_array = (GArray *) g_ptr_array_index(g_tagutils_stack, i);
        taglib_free_tag_array(tag_array);
    }
    g_ptr_array_free(g_tagutils_stack, TRUE);
    g_tagutils_stack = NULL;
    return true;
}

void test(){
    TAGLIB_BEGIN_ADD_TAG(1, "\\data", 0);
    TAGLIB_REQUIRED_TAGS = {"model", NULL};
    TAGLIB_IGNORED_TAGS = {"data", NULL};
    TAGLIB_END_ADD_TAG;
}
