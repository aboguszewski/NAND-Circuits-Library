#include "nand.h"
#include <errno.h>
#include <stdlib.h>

// Służy do określenia jakiego typu jest wskaźnik w strukturze TypedPointer.
typedef enum {
    SIGNAL, // Wskazuje na sygnał bool
    GATE,   // Wskazuje na bramką nand
    EMPTY   // Jest ustawiony na NULL
} Type;

// Reprezentuje wskaźnik na sygnał bool lub bramkę nand.
typedef struct {
    void* pointer; // Wskaźnik na obiekt
    Type type;     // Typ obiektu
} TypedPointer;

struct nand {
    unsigned id;                    // Unikalny identyfikator bramki w bibliotece
    TypedPointer* inputs;           // Tablica wskaźników na wejściowe sygnały/bramki
    unsigned inputs_count;          // Liczba wejść bramki
    nand_t** connected_to;          // Tablica wskaźników na bramki, do których jest podłączona
    size_t connected_to_count;      // Liczba bramek do których jest podłączona
    size_t connected_to_array_size; // Rozmiar tablicy connected_to
};

// Reprezentuje wartość zmiennej bool lub to, że jest niezainicjowana.
typedef enum {
    TRUE,  
    FALSE, 
    NONE   // niezainicjowana
} IsBool;

u_int64_t NAND_ID = 0; // Ostatni przydzielony identyfikator

// Usuwa informację o tym, że wyjście bramki 'g_out' jest podłączone do wejścia bramki 'g_in'.
// Zwraca 'true' jeśli wystąpi błąd.
bool remove_from_connected_to(nand_t* g_out, nand_t* g_in) { 
    if (g_out->connected_to == NULL) {
        g_out->connected_to_array_size = 0;
        g_out->connected_to_count = 1;
        return false;
    }
    
    nand_t** new_connected_to = (nand_t**) malloc(sizeof(nand_t*) * g_out->connected_to_array_size);
    if (new_connected_to == NULL) {
        errno = ENOMEM;
        return true;
    }

    u_int64_t new_index = 0;
    bool gate_removed = false;
    
    for (u_int64_t i = 0; i < g_out->connected_to_count; ++i) {
        if (g_out->connected_to[i] != g_in || gate_removed == true) {
            new_connected_to[new_index++] = g_out->connected_to[i];
        }
        else if (g_out->connected_to[i] == g_in && gate_removed == false) {
            gate_removed = true;
        }
    }
    free(g_out->connected_to);
    g_out->connected_to = new_connected_to;
    return false;
}

// Sprawdza czy bramka pod wskaźnikiem 'g' się zapętla.
// Zwraca 1 (zapętla się), 0 (nie zapętla się) lub -1 (błąd).
int is_loop(nand_t* g, nand_t** tab, u_int64_t last_index, u_int64_t tab_size) {
    nand_t** new_tab = (nand_t**) malloc(sizeof(nand_t*) * tab_size);
    if (new_tab == NULL) {
        errno = ENOMEM;
        return -1;
    }
    u_int64_t new_tab_size = tab_size;
    for (u_int64_t i = 0; i < last_index; ++i) {
        new_tab[i] = tab[i];
    }
    
    if (last_index >= new_tab_size) {
        new_tab_size *= 2;
        nand_t** temp = (nand_t**) realloc(new_tab, sizeof(nand_t*) * new_tab_size);
        if (temp == NULL) {
            errno = ENOMEM;
            free(new_tab);
            return -1;
        }
        new_tab = temp;
    }

    new_tab[last_index] = g;
    
    for (unsigned i = 0; i < g->inputs_count; ++i) {
        for (u_int64_t j = 0; j <= last_index; ++j) {
            if (g->inputs[i].type == GATE && g->inputs[i].pointer == new_tab[j]) {
                free(new_tab);
                return 1;
            }
        }
    }

    ++last_index;

    for (unsigned i = 0; i < g->inputs_count; ++i) {
        if (g->inputs[i].type == GATE) {
            int input_is_loop = is_loop((nand_t*) g->inputs[i].pointer, new_tab, last_index, new_tab_size);
            if (input_is_loop == -1) {
                free(new_tab);
                return -1;
            }
            if (input_is_loop == 1) {
                free(new_tab);
                return 1;
            }
        }
        
    }

    free(new_tab);
    return 0;
}

// Sprawdza, czy bramka pod wskaźnikiem 'g' ma jakieś puste wejścia.
bool has_NULL_input(TypedPointer* g) {
    if (g->pointer == NULL) {
        return true;
    }
    
    if (g->type == GATE) {
        nand_t* inspected_nand = (nand_t*) g->pointer;
        for (unsigned i = 0; i < inspected_nand->inputs_count; ++i) {
            if (has_NULL_input(&inspected_nand->inputs[i]) == true) {
                return true;
            }
        }
    }
    
    return false;
}

// Zwraca większą z 'a' i 'b' lub -1.
ssize_t maximum(ssize_t a, ssize_t b) {
    if (a == -1 || b == -1) {
        return -1;
    }
    return a > b ? a : b;
}

// Zwraca ścieżkę krytyczną bramki pod wskaźnikiem 'g'.
// 'critical_paths' to tablica wyliczonych wcześniej ścieżek krytycznych.
ssize_t evaluate_critical_path(nand_t* g, ssize_t* critical_paths) {
    if (critical_paths[g->id] != -1) {
        return critical_paths[g->id];
    }
    if (g->inputs == 0) {
        return 0;
    }
    
    ssize_t max_critical_path = 0;
    for (unsigned i = 0; i < g->inputs_count; ++i) {
        if (g->inputs[i].pointer == NULL && g->inputs->type == EMPTY) {
            errno = ECANCELED;
            return -1;
        }
        
        if (g->inputs[i].type == GATE) {
            nand_t* inspected_nand = (nand_t*) g->inputs[i].pointer;
            max_critical_path = maximum(
                                max_critical_path,
                                evaluate_critical_path(inspected_nand, critical_paths));
            if (max_critical_path == -1) {
                return -1;
            }
        }
    }

    return max_critical_path + 1;
}

// Zwraca sygnał wyjściowy bramki pod wskaźnikiem 'g'.
// 'output_signals' to tablica wyliczonych wcześniej sygnałów wyjściowych.
bool evaluate_output(nand_t* g, IsBool* output_signals) {    
    if (output_signals[g->id] == TRUE) {
        return true;
    }
    else if (output_signals[g->id] == FALSE) {
        return false;
    }
    
    for (unsigned i = 0; i < g->inputs_count; ++i) {
        bool input_signal;
        if (g->inputs[i].type == SIGNAL) {
            input_signal = *((bool*) g->inputs[i].pointer);
        }
        else {
            nand_t* inspected_nand = (nand_t*) g->inputs[i].pointer;
            input_signal = evaluate_output(inspected_nand, output_signals);
        }

        if (input_signal == false) {
            output_signals[g->id] = TRUE;
            return true;
        }
    }

    output_signals[g->id] = FALSE;
    return false;
}

nand_t* nand_new(unsigned n) {
    nand_t* new_nand = (nand_t*) malloc(sizeof(nand_t));
    if (new_nand == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    
    if (n != 0) {
        new_nand->inputs = (TypedPointer*) malloc(sizeof(TypedPointer) * n);
        if (new_nand->inputs == NULL) {
            errno = ENOMEM;
            free(new_nand);
            return NULL;
        }
        for (unsigned i = 0; i < n; ++i) {
            new_nand->inputs[i].pointer = NULL;
            new_nand->inputs[i].type = EMPTY;
        }
    }
    else {
        new_nand->inputs = NULL;
    }
    
    new_nand->inputs_count = n; 
    new_nand->connected_to = NULL;
    new_nand->connected_to_count = 0;
    new_nand->connected_to_array_size = 0;
    new_nand->id = NAND_ID++;

    return new_nand;
}

void    nand_delete(nand_t *g) {
    if (g == NULL) {
        return;
    }

    if (g->inputs != NULL) {
        free(g->inputs);
    }

    if (g->connected_to != NULL) {
        free(g->connected_to);
    }
    
    free(g);

    return;
}

int     nand_connect_nand(nand_t *g_out, nand_t *g_in, unsigned k) {
    if (g_out == NULL || g_in == NULL || k >= g_in->inputs_count) {
        errno = EINVAL;
        return -1;
    }

    if (g_in->inputs[k].type == GATE) {
        nand_t* removed_nand = (nand_t*) g_in->inputs[k].pointer;
        bool result = remove_from_connected_to(removed_nand, g_in);
        if (result == 1) {
            errno = ENOMEM;
            return -1;
        }
        removed_nand->connected_to_count--;
    }

    g_in->inputs[k] = (TypedPointer) {g_out, GATE};
    g_out->connected_to_count++;

    if (g_out->connected_to_count > g_out->connected_to_array_size) {
        if (g_out->connected_to_array_size == 0) {
            g_out->connected_to_array_size++;
        }
        else {
            g_out->connected_to_array_size *= 2;
        }

        nand_t** temp = (nand_t**) realloc(
                                   g_out->connected_to, 
                                   sizeof(nand_t*) * g_out->connected_to_array_size);
        if (temp == NULL) {
            errno = ENOMEM;
            return -1;
        }

        g_out->connected_to = temp;
    }
    g_out->connected_to[g_out->connected_to_count - 1] = g_in;

    return 0;
}

int     nand_connect_signal(bool const *s, nand_t *g, unsigned k) {
    if (s == NULL || g == NULL || k >= g->inputs_count) {
        errno = EINVAL;
        return -1;
    }

    if (g->inputs[k].type == GATE) {
        nand_t* removed_nand = (nand_t*) g->inputs[k].pointer;
        bool result = remove_from_connected_to(removed_nand, g);
        if (result == 1) {
            errno = ENOMEM;
            return -1;
        }
        removed_nand->connected_to_count--;
    }
    
    g->inputs[k] = (TypedPointer) {(bool*) s, SIGNAL};
    
    return 0;
}

ssize_t nand_fan_out(nand_t const *g) {
    if (g == NULL) {
        errno = EINVAL;
        return -1;
    }

    return g->connected_to_count;    
}

ssize_t nand_evaluate(nand_t **g, bool *s, size_t m) {
    if (g == NULL || *g == NULL || s == NULL || m == 0) {
        errno = EINVAL;
        return -1;
    }

    // Tablica do spamiętywania obliczanych ścieżek krytycznych.
    ssize_t* critical_paths = (ssize_t*) malloc(sizeof(ssize_t) * NAND_ID);
    if (critical_paths == NULL) {
        errno = ENOMEM;
        return -1;
    }
    // Tablica do spamiętywania obliczanych sygnałów wyjściowych.
    IsBool* output_signals = (IsBool*) malloc(sizeof(IsBool) * NAND_ID);
    if (output_signals == NULL) {
        errno = ENOMEM;
        free(critical_paths);
        return -1;
    }
    // Ustawia wartości tablic na niezainicjowane.
    for (size_t i = 0; (u_int64_t) i < NAND_ID; ++i) {
        critical_paths[i] = -1;
        output_signals[i] = NONE; 
    }
    
    ssize_t max_critical_path = 0;
    for (size_t i = 0; i < m; ++i) {
        nand_t** loop_tab = (nand_t**) malloc(sizeof(nand_t*));
        if (loop_tab == NULL) {
            errno = ENOMEM;
            free(critical_paths);
            free(output_signals);
            return -1;
        }
        
        loop_tab[0] = NULL;

        int nand_is_loop = is_loop(g[i], loop_tab, 0, 1);
        if (nand_is_loop == -1) {
            errno = ENOMEM;
            free(loop_tab);
            free(critical_paths);
            free(output_signals);
            return -1;
        }
        if (nand_is_loop == 1) {
            errno = ECANCELED;
            free(loop_tab);
            free(critical_paths);
            free(output_signals);
            return -1;
        }
        free(loop_tab);

        TypedPointer inspected_nand = {g[i], GATE};
        if (has_NULL_input(&inspected_nand) == true) {
            errno = ECANCELED;
            free(critical_paths);
            free(output_signals);
            return -1;
        }

        max_critical_path = maximum(
                            max_critical_path,
                            evaluate_critical_path(g[i], critical_paths));
        if (max_critical_path == -1) {
            free(critical_paths);
            free(output_signals);
            return -1;
        }
        
        s[i] = evaluate_output(g[i], output_signals);
        if (max_critical_path == -1) {
            free(critical_paths);
            free(output_signals);
            return -1;
        }
    }

    free(critical_paths);
    free(output_signals);
    
    return max_critical_path;
}

void*   nand_input(nand_t const *g, unsigned k) {
    if (g == NULL || (ssize_t) k >= g->inputs_count) {
        errno = EINVAL;
        return NULL;
    }

    if (g->inputs[k].pointer == NULL) {
        errno = 0;
        return NULL;
    }

    return g->inputs[k].pointer;
}

nand_t* nand_output(nand_t const *g, ssize_t k) {
    if (g == NULL || (size_t) k >= g->connected_to_count || k == -1) {
        errno = EINVAL;
        return NULL;
    }
    
    return g->connected_to[k];
}