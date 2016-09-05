#include "util.h"
#include "dlfcn.h"
#include "plugins.h"
#include <dirent.h>
#include <SDL_events.h>
#include <SDL_mutex.h>

#define SDLWAITTIMEOUT 2000

pluginHandler *ph;
pthread_mutex_t callMutex=PTHREAD_MUTEX_INITIALIZER;

//  Paramtyp 0 : pointer
//           1 : string
//           2 : PointerStruct *PS
//           3 : DukContext *ctx

bool callEx(char *funcNameTmp, void *ret, void *paramsTmp, int paramType,bool waitThread){
    if(!funcNameTmp){
        slog(ERROR,ERROR,"Invalid function name");
        return false;
    }
    pthread_mutex_lock(&callMutex);
    void *params = NULL;
    char *funcName = strdup(funcNameTmp);
    ph->callCount++;   
    void *(*thr_func)(void *) = ht_get_simple(ph->thr_functions,funcName);
    if(thr_func){
        SDL_Event event = { 0 }; //malloc(sizeof(SDL_Event));
        SDL_zero(event);
        switch(paramType) {
            case CALL_TYPE_PTR:
                if(paramsTmp) {
                    params = paramsTmp;
                    slog(INFO,LOG_PLUGIN,"call( %s(<binary>) )",funcName);
                    event.type = WALLY_CALL_PTR;
                } else { 
                    slog(INFO,LOG_PLUGIN,"call( %s(NULL) )",funcName);
                    event.type = WALLY_CALL_NULL;
                }
                break;
            case CALL_TYPE_STR:
                if(paramsTmp){
                    params = strdup(paramsTmp);
                    slog(INFO,LOG_PLUGIN,"call( %s(%s) )",funcName,params);
                    event.type = WALLY_CALL_STR;
                } else {
                    event.type = WALLY_CALL_NULL;
                }
                break;
            case CALL_TYPE_PS:
                // TODO
                params = paramsTmp;
                slog(DEBUG,LOG_PLUGIN,"call( %s(<PS *>) )",funcName,paramsTmp);
                event.type = WALLY_CALL_PS;
                break;
            case CALL_TYPE_CTX:
                // TODO
                params = paramsTmp;
                event.type = WALLY_CALL_CTX;
                slog(DEBUG,LOG_PLUGIN,"call( %s(<duk_ctx *>) )",funcName,paramsTmp);
                break;
            case CALL_TYPE_PSA:
                // TODO
                params = paramsTmp;
                slog(DEBUG,LOG_PLUGIN,"call( %s(<PSA *>) )",funcName,paramsTmp);
                event.type = WALLY_CALL_PSA;
                break;
 
            default:
                break;
        }
        // The thread loop will free(funcName);
        event.user.data1=funcName;
        event.user.data2=params;
        // We give up the ownership of the funcName copy + and the params copy here
        SDL_TryLockMutex(ph->funcMutex);
        SDL_PushEvent(&event);
        if(waitThread == true){
            // Enable the Mutex code for synced function calls
            slog(TRACE,LOG_PLUGIN,"Wait %d ms until %s has finished.",SDLWAITTIMEOUT,funcName);
            if(SDL_MUTEX_TIMEDOUT == 
                    SDL_CondWaitTimeout(ht_get_simple(ph->functionWaitConditions,funcName),ph->funcMutex,SDLWAITTIMEOUT))
                {
                    slog(ERROR,LOG_PLUGIN,"Wait condition for call %s timed out!",funcName);
                    ph->conditionTimeout++;
                }
        }
        ret=0;
    } else {
        void *(*func)(void *) = ht_get_simple(ph->functions,funcName);
        if(func == NULL && thr_func == NULL) {
           slog(ERROR,LOG_PLUGIN,"Function %s not registered!",funcName);
           return false;
        }
        params = paramsTmp;
        ret = (*func)(params);
        // We give up the ownership of funcName + params here
    }
    pthread_mutex_unlock(&callMutex); 
    return ret;
}
bool callWithData(char *funcname, void *ret, void *params){
    return callEx(funcname,ret,params,CALL_TYPE_PTR,true);
}
bool callWithString(char *funcname, void *ret, char *params){
    return callEx(funcname,ret,params,CALL_TYPE_STR,true);
}
bool call(char *funcname, void *ret, char *params){
    return callEx(funcname,ret,params,CALL_TYPE_STR,true);
}
bool callNonBlocking(char *funcname, int *ret, void *params){
    return callEx(funcname,ret,params,CALL_TYPE_PTR,false);
}

bool exportThreaded(const char *name, void *f){
    if(!ht_contains_simple(ph->functions,(void*)name)){
        ht_insert_simple(ph->thr_functions,(void*)name,f);
        // Enable this code for synced function calls
        ht_insert_simple(ph->functionWaitConditions, (void*)name, SDL_CreateCond());
        slog(DEBUG,LOG_PLUGIN,"Function %s registered (threaded)",name);
        return true;
    }
    slog(ERROR,ERROR,"Function %s is already registered! Only one function allowed.",name);
    return false;
}

bool exportSync(const char *name, void *f){
    //if(!ht_contains(ph->functions,name,strlen(name))){
    if(!ht_contains_simple(ph->functions,(void*)name)){
        ht_insert_simple(ph->functions,(void*)name,f);
        slog(DEBUG,LOG_PLUGIN,"Function %s registered",name);
        return true;
    }
    slog(ERROR,LOG_PLUGIN,"Function %s is already registered! Only one function allowed.",name);
    return false;
}

void wally_put_function(const char *name, int threaded, void *(*f), int args){
    char *ncopy = strdup(name);
    assert(ncopy);
    if(threaded == true){
       // TODO : free
       exportThreaded(ncopy,f);
    } else {
       slog(ERROR,LOG_PLUGIN,"Function %s %p %p %p",ncopy,f,ph,ph->functions);
       slog(DEBUG,LOG_PLUGIN,"FKT_SYNC : %s (%d args)",ncopy,args);
       // TODO : free
       exportSync(ncopy,f);
    }
}
// our own try
void wally_put_function_list(pluginHandler *_ph, function_list_entry *funcs) {
    if(!ph){
        slog(ERROR,LOG_PLUGIN,"PH got lost! Resetting it.");
        ph=_ph;
    }
    const function_list_entry *ent = funcs;
    if (ent != NULL) {
        while (ent->name != NULL) {
            wally_put_function(ent->name, ent->threaded, ent->value, ent->nargs);
            ent++;
        }
    }
}



bool openPlugin(char *path, char* name)
{
    char *(*initPlugin)(void *)=NULL;
    void *handle;
    char *error = NULL;
    void *nameCopy = NULL;
    asprintf((char**)&nameCopy,"%s",name);
    handle = dlopen (path, RTLD_LAZY);
//    slog(LVL_INFO,INFO,"Loading plugin : %s", nameCopy);
    // Save the DL Handle for later, it has to stay open as long as we need the functions
    ht_insert_simple(ph->plugins,nameCopy,handle);
    slog(DEBUG,LOG_PLUGIN,"Saved plugin as %s in plugin map %p",name,ph);
    if (!handle) {
        slog(ERROR,LOG_PLUGIN,"Could not load plugin %s : %s",path,dlerror());
        return false;
    }
    initPlugin = dlsym(handle, "initPlugin");
    if ((error = dlerror()) != NULL)  {
        slog(DEBUG,LOG_PLUGIN,"initPlugin() failed or not found (Error : %s)",error);
        return false;
    } else {
       slog(DEBUG,LOG_PLUGIN,"initPlugin() is now at 0x%x / handle at 0x%x",*initPlugin,handle);
    }
    // Initialize Plugin
    // TODO : Save return + function into command map
    char *r = (*initPlugin)(ph);
    slog(INFO,LOG_PLUGIN,"Plugin loaded : %s", r);
    return true;
}

int cleanupPlugins(void){
    unsigned int key_count = 0;
    char *(*cleanupPlugin)(void *)=NULL;
    void **keys = ht_keys(ph->plugins, &key_count);

    for(int i=0; i < key_count; i++){
        char *name = keys[i];
        void *handle = ht_get_simple(ph->plugins,name);
        char *error = NULL;
        if (!handle) {
            slog(ERROR,LOG_PLUGIN,"Couldnt get plugin handle %s : %s",name,error);
            continue;
        }
        cleanupPlugin = dlsym(handle, "cleanupPlugin");
        if ((error = dlerror()) != NULL)  {
            slog(ERROR,LOG_PLUGIN,"cleanupPlugin(0x%x) failed or not found (Error : %s)",handle,error);
            continue;
        }
        (*cleanupPlugin)(NULL);
        // TODO : dlclose al handles in cleanup
        dlclose(handle);
    }
    return true;
}

int pluginLoader(char *path){
    DIR *d;
    struct dirent *dir;
    int pcount=0;
    d = opendir(path);
    slog(DEBUG,LOG_PLUGIN,"Loading plugins from folder %s",path);
    if (d) {
       while ((dir = readdir(d)) != NULL) {
          unsigned long dlen = strlen(dir->d_name);
          if(dir->d_name[dlen-3] == '.' && dir->d_name[dlen-2] == 's' && dir->d_name[dlen-1] == 'o'){
               slog(DEBUG,LOG_PLUGIN,"Initializing plugin %s.", dir->d_name);
               char *p = NULL;
               asprintf(&p,"%s/%s",path,dir->d_name);
               if(openPlugin(p, dir->d_name)){
                    pcount++;
               }
               free(p);
         }
       }
       closedir(d);
    } else {
        slog(ERROR,LOG_PLUGIN,"Could not open plugin folder %s",path);
        return -1;
    }
    slog(INFO,LOG_PLUGIN,"Loaded %d plugins",pcount);
    ph->pluginCount = pcount;
    ph->pluginLoaderDone = true;
    return 0;
}

pluginHandler *pluginsInit(void){

    ph=malloc(sizeof(pluginHandler));
    memset(ph,sizeof(pluginHandler),0);
    pthread_mutex_init(&callMutex,0);

    if(!ph) {
        slog(ERROR,LOG_PLUGIN,"Could not allocate memory. Exit");
        exit(1);
    }
    // TODO : Free
    ph->mainThread = pthread_self();
    ph->pluginLoaderDone = false;

    // Setup defaults in the structure
    ph->autorender = true;
    ph->disableVideo = false;
    ph->disableAudio = false;
    ph->SDL = false;
    ph->playVideo = false;
    ph->broadcomInit = false;
    ph->daemonizing = true;
    ph->ssdp = false;
    ph->cloud = false;
    ph->location = NULL;

    ph->funcMutex = SDL_CreateMutex();
    ph->functionWaitConditions= malloc(sizeof(hash_table));
    ph->callbacks= malloc(sizeof(hash_table));
    ph->thr_functions = malloc(sizeof(hash_table));
    ph->functions = malloc(sizeof(hash_table));
    ph->plugins = malloc(sizeof(hash_table));
    ph->baseTextures = malloc(sizeof(hash_table));
    ph->fonts = malloc(sizeof(hash_table));
    ph->colors = malloc(sizeof(hash_table));
    ph->configMap = malloc(sizeof(hash_table));
    ph->configFlagsMap = malloc(sizeof(hash_table));
    ht_init(ph->functionWaitConditions, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->callbacks, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->thr_functions, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->functions, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->plugins, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->baseTextures, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->fonts, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->colors, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->configMap, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    ht_init(ph->configFlagsMap, HT_KEY_CONST | HT_VALUE_CONST, 0.05);
    return ph;
}

int sendWallyCommand(char *cmd, char *log){
    FILE *f = fopen(FIFO, "a");
    if(!f) return false;
    fprintf(f, "%s %s\n",cmd, log);
    return fclose(f);
}
