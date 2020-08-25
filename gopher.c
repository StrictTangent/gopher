// Gopher - Terminal Based File Explorer w/ Ncurses
// Paul Freeman - 2020


#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <menu.h>
#include <ctype.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>


#define MAXLEN 800
#define MAXITEMS 50000
#define MENUWIDTH_MAX 120

#define MENUHEIGHT_MAX 40

int MENUHEIGHT = 20;
int MENUWIDTH = 100;

#define X_OFFSET 4
#define Y_OFFSET 1
#define MSGWIDTH MENUWIDTH - 4

int ALLOW_INTERRUPT = 1;

// These aren't really necessary
int USE_OPTIONS_MENU = 1;
int IN_OPTIONS_MENU = 0;

// Struct for the filelist
typedef struct {
  char * name;
  char * name_short;
  char * type;
  mode_t st_mode;
  
  char * size;
  size_t bytes;
  char * description;
  char * mod_date;
  time_t mod_time;
} file_info;

enum compressors {ZIP = 2000,
                  UNZIP,
                  TAR,
                  UNTAR};

void print_in_middle(WINDOW *win, int starty, int startx, int width, char *string, chtype color);
void sortfiles(file_info ** filelist, int count, int (*func)(const void * ptr1, const void * ptr2));
void refresh_menu(file_info ** filelist, ITEM** menu_items, int n_choices, MENU ** dir_menu, WINDOW ** dir_menu_win, char * dirbuff);
int filecomp_size(const void * ptr1, const void * ptr2);
int filecomp_name_desc(const void * ptr1, const void * ptr2);
int filecomp_size_desc(const void * ptr1, const void * ptr2);
int filecomp_name(const void * ptr1, const void * ptr2);
int filecomp_date(const void * ptr1, const void * ptr2);
int filecomp_date_desc(const void * ptr1, const void * ptr2);
void destroy_filelist(file_info ** filelist);
void clear_filelist(file_info ** filelist);
ITEM * get_lettered_item(ITEM ** menu_items, ITEM * current, int num_items, char c);
int build_filelist(file_info ** filelist, DIR * dfd);
void refresh_littlebox(char * msg);

int present_options(WINDOW ** dir_menu_win, MENU ** opt_menu, WINDOW ** opt_menu_win, ITEM ** opt_items, file_info * current_file_info, int item_no);
void run_prog(file_info * current_file_info, char * prog_name);
void rename_file(file_info * current_file_info);
int new_dir();
void file_touch();
void open_terminal(char * dirbuff);
void handle_winch(int sig);
void executecommand(char * msgbuff);
void unzip(file_info * current_file_info, char * msgbuff);
void extract_tar(file_info * current_file_info, char * msgbuff);
void zip(file_info * current_file_info, char * msgbuff);
void compress_tar(file_info * current_file_info, char * msgbuff);

char  ** arg_parse (char *line, int *argcptr);

// Global pointer copies for use by signal handler "handle_winch"
// (Consider Just making the originals global...)
file_info ** sig_filelist;
ITEM ** sig_menu_items;
int * sig_nchoices;
MENU ** sig_dir_menu;
WINDOW ** sig_dir_menu_win;
char * sig_dirbuff;


// Function Pointer for current comparator.
// Kept global for use by handle_winch
int (*comp_func)(const void *, const void *);


int main() {
  ITEM ** menu_items;
  if ((menu_items = calloc(MAXITEMS, sizeof(ITEM *))) == NULL){
    perror("calloc");
    exit(errno);
  } 

  int c, opt_ret;
  MENU * dir_menu = NULL;
  WINDOW * dir_menu_win;
  int n_choices, i;

  //optionsmenu
  MENU * opt_menu = NULL;
  WINDOW * opt_menu_win;
  ITEM ** opt_items;
  if ((opt_items = calloc(20, sizeof(ITEM *))) == NULL){
    perror("calloc");
    exit(errno);
  }
  
  //Current sort function pointer
  //int (*comp_func)(const void *, const void *) = &filecomp_name;
  comp_func = &filecomp_name;

  initscr();
  start_color();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  init_pair(1, COLOR_RED, COLOR_BLACK);
  init_pair(2, COLOR_CYAN, COLOR_BLACK);
  init_pair(3, COLOR_MAGENTA, COLOR_BLACK);

   

  char msgbuff[MSGWIDTH - 2];
  sprintf(&msgbuff[MSGWIDTH -6], "...");
  
  char dirbuff[MAXLEN];
  getcwd(dirbuff, MAXLEN);
  int item_no;
  DIR * dfd;
  pid_t cpid;
  int status;

  if (!(dfd = opendir(dirbuff))){
    fprintf(stderr, "Can't open directory\n");
  }
  file_info ** filelist;
  if ((filelist= calloc(MAXITEMS, sizeof(file_info *))) == NULL){
    perror("calloc");
    exit(errno);
  }
  
  char clipboard[MAXLEN];
  clipboard[0] = 0;
  char * copy_args[5];
  copy_args[0] = "cp";
  copy_args[1] = "-r";
  copy_args[2] = NULL;
  copy_args[3] = NULL;
  copy_args[4] = NULL;

  char * del_args[4];
  del_args[0] = "rm";
  del_args[1] = "-rf";
  del_args[3] = NULL;

  int CHANGEDIR;

  //logfile
  struct passwd *pw = getpwuid(getuid());
  const char *homedir = pw->pw_dir;
  char logdir[200];
  sprintf(logdir, "%s/.gopherlog", homedir);
  int log_fd = open(logdir, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  dup2(log_fd, 2);


  //Set up signal handler for resize
  struct sigaction sa;
  memset(&sa, 0, sizeof(struct sigaction));
  sa.sa_handler = handle_winch;
  sigaction(SIGWINCH, &sa, NULL);
  
  sig_filelist = filelist;
  sig_menu_items = menu_items;
  sig_nchoices = &n_choices;
  sig_dir_menu = &dir_menu;
  sig_dir_menu_win = &dir_menu_win;
  sig_dirbuff = dirbuff;
  //End signal handler setup
  
  while(1){

    MENUHEIGHT = LINES - 6;
    MENUWIDTH = COLS - 6;
    MENUWIDTH = MENUWIDTH > MENUWIDTH_MAX ? MENUWIDTH_MAX : MENUWIDTH;
    MENUHEIGHT = MENUHEIGHT > MENUHEIGHT_MAX ? MENUHEIGHT_MAX : MENUHEIGHT;
    
    CHANGEDIR= 0;

    n_choices = build_filelist(filelist, dfd);
    sortfiles(filelist, n_choices, comp_func);
    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);


    // HANDLE KEYBOARD INPUT
    opt_ret = -1;
    //c = wgetch(dir_menu_win);
    c = -1;
    while(c != KEY_F(1)){
      opt_ret = -1;
      if (c == -1) c = wgetch(dir_menu_win);
      if (c == KEY_F(1)) break;
      
      //fprintf(stderr, "KEY PRESS IS %d\n", c);
      ITEM * curr;
      int abort = 0;
      // a - z => go to next item starting with that letter
      if (c >= 'a' && c <= 'z'){
	      set_current_item(dir_menu, get_lettered_item(menu_items, current_item(dir_menu), n_choices, c));

      } else {
	switch(c)
	  {
	  case KEY_DOWN:
	    if (item_index(current_item(dir_menu)) == n_choices - 1){
	      menu_driver(dir_menu, REQ_FIRST_ITEM);
	    } else {
	      menu_driver(dir_menu, REQ_DOWN_ITEM);
	    }
	    //menu_driver(dir_menu, REQ_DOWN_ITEM);
	    refresh_littlebox(filelist[item_index(current_item(dir_menu))]->name);
	    break;
	  case KEY_UP:
	    if (item_index(current_item(dir_menu)) == 0){
	      menu_driver(dir_menu, REQ_LAST_ITEM);
	    } else {
	      menu_driver(dir_menu, REQ_UP_ITEM);
	    }
	    //menu_driver(dir_menu, REQ_UP_ITEM);
	    refresh_littlebox(filelist[item_index(current_item(dir_menu))]->name);

	    break;
	  case KEY_NPAGE:
	    menu_driver(dir_menu, REQ_SCR_DPAGE);
	    break;
	  case KEY_PPAGE:
	    menu_driver(dir_menu, REQ_SCR_UPAGE);
	    break;


	    ////////////ENTER////////////////////////////////////////////////
	  case 10:
	    curr = current_item(dir_menu);
	    item_no = item_index(curr);
	    if (item_no > 0){
	      //curr = current_item(dir_menu);
	      //item_no = item_index(curr);
	      
	      opt_ret = present_options(&dir_menu_win, &opt_menu, &opt_menu_win, opt_items, filelist[item_no], item_no);
	      break;


	    } else {
	      //curr = current_item(dir_menu);
	      //item_no = item_index(curr);
	      if (S_ISDIR(filelist[item_no]->st_mode)){
		chdir(filelist[item_no]->name);
		getcwd(dirbuff, MAXLEN);
		if (!(dfd = opendir(dirbuff))){
		  fprintf(stderr, "Can't open directory\n");
		} else {
		  CHANGEDIR = 1;
		}
	      }
	      break;
	    }
	    break;
	    /////////////////////////////////////////////////////////////////////////////

	  case KEY_RIGHT:
	    curr = current_item(dir_menu);
	      item_no = item_index(curr);
	      if (S_ISDIR(filelist[item_no]->st_mode)){
		chdir(filelist[item_no]->name);
		getcwd(dirbuff, MAXLEN);
		if (!(dfd = opendir(dirbuff))){
		  fprintf(stderr, "Can't open directory\n");
		} else {
		  CHANGEDIR = 1;
		}
	      }
	      break;
	    
	  case KEY_LEFT:
	  
	    item_no = 0;
	    if (S_ISDIR(filelist[item_no]->st_mode)){
	      chdir(filelist[item_no]->name);
	      getcwd(dirbuff, MAXLEN);
	      if (!(dfd = opendir(dirbuff))){
		fprintf(stderr, "Can't open directory\n");
	      } else {
		CHANGEDIR = 1;
	      }
	    }
	    break;

	  case 'S': //'=':

	    comp_func = comp_func == &filecomp_size ? &filecomp_size_desc : &filecomp_size;
	    
	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    break;

	  case 'A': //'-':
	    
	    comp_func = comp_func == &filecomp_name ? &filecomp_name_desc : &filecomp_name;

	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    break;

	  case 'D':

	    comp_func = comp_func == &filecomp_date ? &filecomp_date_desc : &filecomp_date;

	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    break;

	  case 'C': //shift + c COPY
	    
	    item_no = item_index(current_item(dir_menu));
	    if (item_no == 0) break;
	    if (strlen(dirbuff) + strlen(filelist[item_no]->name) + 2 < MAXLEN){
	      
	      if (snprintf(clipboard, MAXLEN, "%s/%s", dirbuff, filelist[item_no]->name) >= MAXLEN){
		refresh_littlebox("Unable to copy file...");
		clipboard[0] = 0;
		break;
	      }
	      copy_args[0] = "cp";
	      copy_args[1] = "-rf";
	      if (copy_args[2]) free(copy_args[2]);
	      if (copy_args[3]) free(copy_args[3]);
	      copy_args[2] = strdup(clipboard);
	      
	      if (snprintf(msgbuff, MSGWIDTH - 2, "Copied to Clipboard: %s", clipboard) > MSGWIDTH -2){
		// Do nothing
	      }
	      
	      if (strlen(msgbuff) >= MSGWIDTH - 3) sprintf(&msgbuff[MSGWIDTH -6], "...");
	      
	      refresh_littlebox(msgbuff);

	    }
	    break;

	  case 'X':
	    item_no = item_index(current_item(dir_menu));
	    if (item_no == 0) break;
	    if (strlen(dirbuff) + strlen(filelist[item_no]->name) + 2 < MAXLEN){
	      
	      if (snprintf(clipboard, MAXLEN, "%s/%s", dirbuff, filelist[item_no]->name) >= MAXLEN){
		refresh_littlebox("Unable to move file...");
		clipboard[0] = 0;
		break;
	      }
	      copy_args[0] = "mv";
	      copy_args[1] = "-f";
	      if (copy_args[2]) free(copy_args[2]);
	      if (copy_args[3]) free(copy_args[3]);
	      copy_args[2] = strdup(clipboard);
	      
	      if (snprintf(msgbuff, MSGWIDTH - 2, "Moved to Clipboard: %s", clipboard) > MSGWIDTH -2){
		// Do nothing
	      }
	      
	      if (strlen(msgbuff) >= MSGWIDTH - 3) sprintf(&msgbuff[MSGWIDTH -6], "...");
	      
	      refresh_littlebox(msgbuff);

	    }
	    break;
	    
	  case 'V': //shift + v PASTE

	    if (clipboard[0] == 0) break;

	    //find filename of file to be copied
	    i = strlen(clipboard) - 1;
	    while (clipboard[i] != '/'){
	      i--;
	    }
	    i++;
	    fprintf(stderr, "name to copy is: %s\n", &clipboard[i]);

	    copy_args[3] = strdup(dirbuff); // directory to copy/move to
	    for (item_no = 0; item_no < n_choices; item_no++){
	      if (!strcmp(&clipboard[i], filelist[item_no]->name)){
          ALLOW_INTERRUPT = 0;
		refresh_littlebox("Filename already exists. (r)ename, (o)verwrite, or (a)bort...");
		while (1){
      refresh_littlebox("Filename already exists. (r)ename, (o)verwrite, or (a)bort...");
		  c = getch();
		  if (c == 'r'){
		    refresh_littlebox("New Name: ");
		    echo();
		    curs_set(1);
		    if (getstr(msgbuff) == ERR){
          curs_set(0);
		      noecho();
          abort = 1;
          refresh_littlebox("Whoops! Did not copy file");
          
        }
		    curs_set(0);
		    noecho();
		    free(copy_args[3]);
		    for (item_no = 0; item_no < n_choices; item_no++){
		      if (!strcmp(msgbuff, filelist[item_no]->name)) {
			refresh_littlebox("That name already exists. Failed to write file");
			abort = 1;
			break;
		      }
		    }
		    copy_args[3] = strdup(msgbuff);
		    break;
		  } else if (c == 'a'){
		    refresh_littlebox("Did not copy file");
		    
		    abort = 1;
		    break;
		  } else if (c == 'o'){
		    break;
		  }
      
		}
		
		break;
	      }
	    }
	    if (abort) {
	      free(copy_args[3]);
	      copy_args[3] = NULL;
	      break;
	    }
	    //copy_args[3] = strdup(dirbuff); // directory to copy/move to
	    
	    cpid = fork();
      
	    if (cpid == 0){ //we are child
	
	      execvp(copy_args[0], copy_args);
	      
	      perror("exec");
	      
	      
	      fclose(stdin);
	      exit(127);
	    }
	    if (copy_args[0][0] == 'c')
	      refresh_littlebox("Copying...");
	    else
	      refresh_littlebox("Moving...");
	    
	    while (waitpid(cpid, &status, 0) < 0){
	      perror("wait");
	    }
	    if (!(dfd = opendir(dirbuff))){
	      fprintf(stderr, "Can't open directory\n");
	    }
	    
	    n_choices = build_filelist(filelist, dfd);
	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    
	    if (copy_args[0][0] == 'c') {
	      refresh_littlebox("File copied");
	    } else {
	      refresh_littlebox("File moved");
	      clipboard[0] = 0;
	    }
      ALLOW_INTERRUPT = 1;
	    break;

	  case KEY_DC: // DELETE
	    item_no = item_index(current_item(dir_menu));
	    
	    if (!strcmp(filelist[item_no]->name, "..")){
	      move(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4);
	      clrtoeol();
	      mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + MENUWIDTH - 1, ACS_VLINE);
	      attron(COLOR_PAIR(1));
        
	      mvprintw(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4, "Cannot delete parent directory! (Press any key to continue)", filelist[item_no]->name);
	      attroff(COLOR_PAIR(1));
	      getch();
        
	      move(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4);
	      clrtoeol();
	      mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + MENUWIDTH - 1, ACS_VLINE);
	      refresh();
	      break;
	    }

	    move(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4);
	    clrtoeol();
	    mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + MENUWIDTH - 1, ACS_VLINE);
	    attron(COLOR_PAIR(1));
	    //mvprintw(MENUHEIGHT + Y_OFFSET + 3, X_OFFSET + 1, "Are you SURE you wish to delete this file? (y/n)");
      ALLOW_INTERRUPT = 0;
	    mvprintw(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4, "Are you SURE you wish to delete this file? (y/n)");
	    //refresh_littlebox("Are you SURE you wish to delete this file? (y/n)");
	    attroff(COLOR_PAIR(1));
	    do {
	      c = getch();
	    } while(c != 'y' && c != 'n');
      ALLOW_INTERRUPT = 1;
	    if (c != 'y') {
	      move(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4);
	      clrtoeol();
	      mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + MENUWIDTH - 1, ACS_VLINE);
	      refresh();
	      break;
	    }
	    move(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4);
	    clrtoeol();
	    mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + MENUWIDTH - 1, ACS_VLINE);
	    refresh();
	    
	    del_args[2] = strdup(filelist[item_no]->name);
	    
	    cpid = fork();
      
	    if (cpid == 0){ //we are child
	      execvp(del_args[0], del_args);
	      //execvp(copy_args[0], copy_args);

	      perror("exec");
	      fclose(stdin);
	      exit(127);
	    }
	  
	    if (waitpid(cpid, &status, 0) < 0){
	      perror("wait");
	    }
	    free(del_args[2]);
	    if (!(dfd = opendir(dirbuff))){
	      fprintf(stderr, "Can't open directory\n");
	    }
	    n_choices = build_filelist(filelist, dfd);
	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    set_current_item(dir_menu, menu_items[item_no]);
	    break;


	  case 'R':
	    rename_file(filelist[item_index(current_item(dir_menu))]);
	    if (!(dfd = opendir(dirbuff))){
	      fprintf(stderr, "Can't open directory\n");
	    }
	    n_choices = build_filelist(filelist, dfd);
	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    break;


	  case 'M':
	    if (new_dir()){
	      strcpy(msgbuff, strerror(errno));
	    } else {
	      strcpy(msgbuff, "Success");
	    }
	    if (!(dfd = opendir(dirbuff))){
	      fprintf(stderr, "Can't open directory\n");
	    }
	    n_choices = build_filelist(filelist, dfd);
	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    refresh_littlebox(msgbuff);
	    break;


	  case 'N':
	    file_touch();
	    if (!(dfd = opendir(dirbuff))){
	      fprintf(stderr, "Can't open directory\n");
	    }
	    n_choices = build_filelist(filelist, dfd);
	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    break;

	  case 'T':
      ALLOW_INTERRUPT = 0;
	    endwin();
	    open_terminal(dirbuff);
	    if (!(dfd = opendir(dirbuff))){
	      fprintf(stderr, "Can't open directory\n");
	    }
      ALLOW_INTERRUPT = 1;
	    n_choices = build_filelist(filelist, dfd);
	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
	    refresh();
	    break;

	  case 'E':
	    executecommand(msgbuff);
	    if (!(dfd = opendir(dirbuff))){
	      fprintf(stderr, "Can't open directory\n");
	    }
	    n_choices = build_filelist(filelist, dfd);
	    sortfiles(filelist, n_choices, comp_func);
	    refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
      refresh_littlebox(msgbuff);
	    break;

      case UNZIP:
      
        unzip(filelist[item_index(current_item(dir_menu))], msgbuff);
        if (!(dfd = opendir(dirbuff))){
	        fprintf(stderr, "Can't open directory\n");
	      }
	      n_choices = build_filelist(filelist, dfd);
	      sortfiles(filelist, n_choices, comp_func);
	      refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
        refresh_littlebox(msgbuff);
        break;

        case UNTAR:
        extract_tar(filelist[item_index(current_item(dir_menu))], msgbuff);
        if (!(dfd = opendir(dirbuff))){
	        fprintf(stderr, "Can't open directory\n");
	      }
	      n_choices = build_filelist(filelist, dfd);
	      sortfiles(filelist, n_choices, comp_func);
	      refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
        refresh_littlebox(msgbuff);
        break;

      case ZIP:
        zip(filelist[item_index(current_item(dir_menu))], msgbuff);
        if (!(dfd = opendir(dirbuff))){
	        fprintf(stderr, "Can't open directory\n");
	      }
	      n_choices = build_filelist(filelist, dfd);
	      sortfiles(filelist, n_choices, comp_func);
	      refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
        refresh_littlebox(msgbuff);
        break;

      case TAR:
        compress_tar(filelist[item_index(current_item(dir_menu))], msgbuff);
        if (!(dfd = opendir(dirbuff))){
	        fprintf(stderr, "Can't open directory\n");
	      }
	      n_choices = build_filelist(filelist, dfd);
	      sortfiles(filelist, n_choices, comp_func);
	      refresh_menu(filelist, menu_items, n_choices, &dir_menu, &dir_menu_win, dirbuff);
        refresh_littlebox(msgbuff);
        break;
	  }

	
      
      ////////////END SWITCH//////////////////
      
	if (CHANGEDIR) {
	  break;
	}
      }
      if (opt_ret != -1) {
	c = opt_ret;
      } else {
	c = -1;
      }
      wrefresh(dir_menu_win);
    }

    if(!CHANGEDIR) break;
    
			 
  }
  //CLEANUP
  unpost_menu(dir_menu);
  free_menu(dir_menu);
  dir_menu = NULL;
  for(i = 0; i < MAXITEMS; i++){
    free_item(menu_items[i]);
  }
  free(menu_items);
  free(opt_items);
  destroy_filelist(filelist);
  if (copy_args[2]) free(copy_args[2]);
  if (copy_args[3]) free(copy_args[3]);
  
  endwin();
  return 0;
}

void print_in_middle(WINDOW *win, int starty, int startx, int width, char *string, chtype color)
{	int length, x, y;
	float temp;

	if(win == NULL)
		win = stdscr;
	getyx(win, y, x);
	if(startx != 0)
		x = startx;
	if(starty != 0)
		y = starty;
	if(width == 0)
		width = 80;

	length = strlen(string);
	temp = (width - length)/ 2;
	x = startx + (int)temp;
	wattron(win, color);
	mvwprintw(win, y, x, "%s", string);
	wattroff(win, color);
	refresh();
}

// Comparator - Sort by filename ascending
int filecomp_name(const void * ptr1, const void * ptr2){
  char * A =  (*(file_info **) ptr1)->name;
  char * B =  (*(file_info **) ptr2)->name;

  char a;
  char b;
  
  while(*A != 0 && *B != 0){
    a = tolower(*A);
    b = tolower(*B);

    if (a > b) return 1;
    if (a < b) return -1;
    A++;
    B++;
  }
  if (*A == *B){ //both null
    return 0;
  }
  if (*A == 0) return -1;
  return 1;
}

// Comparator - Sort by date decending
int filecomp_name_desc(const void * ptr1, const void * ptr2){
  return filecomp_name(ptr1, ptr2) * -1;
}

// Comparator - Sort by size ascending
int filecomp_size(const void * ptr1, const void * ptr2){
  size_t A =  (*(file_info **) ptr1)->bytes;
  size_t B =  (*(file_info **) ptr2)->bytes;

  if (A > B) return 1;
  if (A < B) return -1;
  return 0;
}

// Comparator - Sort by size decending
int filecomp_size_desc(const void * ptr1, const void * ptr2){
  return filecomp_size(ptr1, ptr2) * -1;
}

// Comparator - Sort by date ascending
int filecomp_date(const void * ptr1, const void * ptr2){
  time_t A =  (*(file_info **) ptr1)->mod_time;
  time_t B =  (*(file_info **) ptr2)->mod_time;
  if (A > B) return 1;
  if (A < B) return -1;
  return 0;
}

// Comparator - Sort by date descending
int filecomp_date_desc(const void * ptr1, const void * ptr2){
  return filecomp_date(ptr1, ptr2) * -1;
}

// Sorts the filelist using qsort() and a given comparator.
void sortfiles(file_info ** filelist, int count, int (*func)(const void * ptr1, const void * ptr2)){
  filelist++;
  count--;
  qsort(filelist, count, sizeof(file_info *), func);
}

// Presents and controls the dropdown options menu
int present_options(WINDOW ** dir_menu_win, MENU ** opt_menu, WINDOW ** opt_menu_win, ITEM ** opt_items, file_info * current_file_info, int item_no){
  char ** options;
  int i;
  int c;
  int ret = -1;

  int x, y;
  
  getyx(*dir_menu_win, y, x);
  
  y--;

  if (item_no >= MENUHEIGHT - 4) item_no = MENUHEIGHT - 5;
  
  int n_choices;
  char * f_options[] = {"OPEN",
		       "COPY",
		       "MOVE",
		       "DELETE",
		       "RENAME",		       
			"------------",
			"PASTE",
			"NEW FILE",
			"MAKE DIR",
			"TERMINAL",
      "------------",
      "compress as:", //11
      "ZIP",
      "TAR.GZ",
      "------------",
			"BACK", //15
		       NULL,
           NULL};
  n_choices = 16;
  if (strstr(current_file_info->name, ".tar.gz")){
    f_options[11] = "EXTRACT HERE";
    f_options[12] = f_options[14];
    f_options[13] = f_options[15];
    f_options[14] = NULL;
    n_choices = 14;
  } else if (strstr(current_file_info->name, ".zip")){
    f_options[11] = "UNZIP HERE";
    f_options[12] = f_options[14];
    f_options[13] = f_options[15];
    f_options[14] = NULL;
    n_choices = 14;
  }
  

  char * d_options[] = {"OPEN",
		       "COPY",
		       "MOVE",
		       "DELETE",
		       "RENAME",
		       "PROPERTIES",
		       "BACK",
		       NULL};
  
  // REGULAR FILE
  if (S_ISDIR(current_file_info->st_mode)) {
    //options = d_options;
    options = f_options;
  } else {
    options = f_options;
  }
  
  i = 0;
  while (options[i]){
    opt_items[i] = new_item(options[i], "");
    i++;
  }
  opt_items[i] = NULL;
  
  //Create menu
  *opt_menu = new_menu((ITEM **) opt_items);
  
  //Create window
  *opt_menu_win = newwin(10, 14, Y_OFFSET  + y, X_OFFSET + 33);
  keypad(*opt_menu_win, TRUE);
  
  set_menu_win(*opt_menu, *opt_menu_win);
  set_menu_sub(*opt_menu, derwin(*opt_menu_win, 8, 12, 1, 1));
  set_menu_format(*opt_menu, 8, 1);
  set_menu_mark(*opt_menu, "");

  set_menu_fore(*opt_menu, COLOR_PAIR(3) | A_REVERSE);
  set_menu_back(*opt_menu, COLOR_PAIR(2));
  //set_menu_fore(*opt_menu, COLOR_PAIR(3));
  wattron(*opt_menu_win, COLOR_PAIR(2));
  box(*opt_menu_win, 0,0 );
  wattroff(*opt_menu_win, COLOR_PAIR(2));
  
  //post menu
  post_menu(*opt_menu);
  wrefresh(*opt_menu_win);
  IN_OPTIONS_MENU = 1;
  
  
  ITEM * curr;
  int loop = 1;
  char prog_name[80];
  //while((c = wgetch(*opt_menu_win)) != KEY_F(1)){
  while (loop){
    c = wgetch(*opt_menu_win);
    curr = current_item(*opt_menu);
    if (c >= 'a' && c <= 'z'){
      set_current_item(*opt_menu, get_lettered_item(opt_items, current_item(*opt_menu), n_choices, c));
    }
    switch(c)
      {
      case KEY_LEFT:
	ret = -1;
	loop = 0;
	break;
      case  KEY_F(1):
	ret = c;
	loop = 0;
	break;
      case KEY_DOWN:
	if (item_index(current_item(*opt_menu)) == n_choices - 1){
	  menu_driver(*opt_menu, REQ_FIRST_ITEM);
	} else {
	  menu_driver(*opt_menu, REQ_DOWN_ITEM);
	}
	break;
      case KEY_UP:
	if (item_index(current_item(*opt_menu)) == 0){
	  menu_driver(*opt_menu, REQ_LAST_ITEM);
	} else {
	  menu_driver(*opt_menu, REQ_UP_ITEM);
	}
	break;

      case 10:
	if (!strcmp(item_name(curr), "BACK")){
	  ret = -1;
	} else if (!strcmp(item_name(curr), "OPEN")) {
	  if (S_ISDIR(current_file_info->st_mode)){
	    //ret = KEY_ENTER;
	    ret = KEY_RIGHT;
	    
	  } else {
	    refresh_littlebox("Open with: ");
	    echo();
	    curs_set(1);
	    getstr(prog_name);
	    curs_set(0);
	    noecho();

      ALLOW_INTERRUPT = 0;
	    endwin();
	    run_prog(current_file_info, prog_name);
	    keypad(*dir_menu_win, TRUE);
	    
	    refresh();
      ALLOW_INTERRUPT = 1;

	    
	  }
	} else if (!strcmp(item_name(curr), "COPY")) {
	  ret = 'C';
	} else if (!strcmp(item_name(curr), "MOVE")) {
	  ret = 'X';
	} else if (!strcmp(item_name(curr), "DELETE")) {
	  ret = KEY_DC;
	} else if (!strcmp(item_name(curr), "RENAME")) {
	  ret = 'R';
	} else if (!strcmp(item_name(curr), "PASTE")) {
	  ret = 'V';
	} else if (!strcmp(item_name(curr), "NEW FILE")) {
	  ret = 'N';
	} else if (!strcmp(item_name(curr), "MAKE DIR")) {
	  ret = 'M';
	} else if (!strcmp(item_name(curr), "TERMINAL")) {
	  ret = 'T';
	} else if (!strcmp(item_name(curr), "ZIP")){
    ret = ZIP;
  } else if (!strcmp(item_name(curr), "UNZIP HERE")){
    ret = UNZIP;
  } else if (!strcmp(item_name(curr), "EXTRACT HERE")) {
    ret = UNTAR;
  } else if (!strcmp(item_name(curr), "TAR.GZ")) {
    ret = TAR;
  } else {
	  break;
	}
	loop = 0;
	break;
	
      }
  }

  unpost_menu(*opt_menu);
  wclear(*opt_menu_win);
  wrefresh(*opt_menu_win);
  free_menu(*opt_menu);
  redrawwin(*dir_menu_win);
  i = 0;
  while (opt_items[i]){
    free_item(opt_items[i]);
    i++;
  }
  
  return ret;
}

// Refreshes the main menu using the contents of filelist
void refresh_menu(file_info ** filelist, ITEM** menu_items, int n_choices, MENU ** dir_menu, WINDOW ** dir_menu_win, char * dirbuff){
  int i = 0;

  if(*dir_menu){
    unpost_menu(*dir_menu);
    free_menu(*dir_menu);
  }
  while (menu_items[i] != NULL && i < MAXITEMS){
    free_item(menu_items[i]);
    menu_items[i] = NULL;
    i++;
  }

  for(i = 0; i < n_choices; i++){
    menu_items[i] = new_item(filelist[i]->name_short, filelist[i]->description);
  }
  menu_items[n_choices] = NULL;

  // Create menu
  *dir_menu = new_menu((ITEM **) menu_items);
  
  // Create window
  *dir_menu_win = newwin(MENUHEIGHT, MENUWIDTH, Y_OFFSET, X_OFFSET);
  keypad(*dir_menu_win, TRUE);
  
  // Set main window and subwindow
  set_menu_win(*dir_menu, *dir_menu_win);
  set_menu_sub(*dir_menu, derwin(*dir_menu_win, MENUHEIGHT - 4, MENUWIDTH - 2, Y_OFFSET + 2/*3*/, X_OFFSET - 3/*1*/));
  //set_menu_sub(*dir_menu, derwin(*dir_menu_win, MENUHEIGHT - Y_OFFSET, MENUWIDTH - 2, Y_OFFSET - 1/*3*/, X_OFFSET - 3/*1*/));
  set_menu_format(*dir_menu, MENUHEIGHT - 4, 1);
  //set_menu_format(*dir_menu, MENUHEIGHT - Y_OFFSET, 1);
  
  //set_menu_mark(dir_menu, " * ");
  set_menu_mark(*dir_menu, "-> ");
  
  // border and title
  box(*dir_menu_win, 0,0 );
  //print_in_middle(dir_menu_win, 1, 0, MENUWIDTH, dirbuff, COLOR_PAIR(1));
  
  //wattron(*dir_menu_win, COLOR_PAIR(1));
  mvwprintw(*dir_menu_win, 1, 4, "%s", dirbuff);
  mvwprintw(*dir_menu_win, 0, MENUWIDTH - 8, "Gopher");
  
  //wattroff(*dir_menu_win, COLOR_PAIR(2));


  clear();
  refresh();
  
  mvwaddch(*dir_menu_win, 2, 0, ACS_LTEE);
  mvwhline(*dir_menu_win, 2, 1, ACS_HLINE, MENUWIDTH - 2);
  mvwaddch(*dir_menu_win, 2, MENUWIDTH - 1, ACS_RTEE);

  // box for filename
  mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET, ACS_VLINE);
  mvaddch(MENUHEIGHT + Y_OFFSET , X_OFFSET, ACS_ULCORNER);
  mvaddch(MENUHEIGHT + Y_OFFSET + 2, X_OFFSET, ACS_LLCORNER);
  mvhline(MENUHEIGHT + Y_OFFSET + 2, X_OFFSET + 1, ACS_HLINE, MENUWIDTH - 2);
  mvhline(MENUHEIGHT + Y_OFFSET , X_OFFSET + 1, ACS_HLINE, MENUWIDTH - 2);
  mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + MENUWIDTH - 1, ACS_VLINE);
  mvaddch(MENUHEIGHT + Y_OFFSET , X_OFFSET + MENUWIDTH - 1, ACS_URCORNER);
  mvaddch(MENUHEIGHT + Y_OFFSET +2, X_OFFSET + MENUWIDTH - 1, ACS_LRCORNER);
  
  //post menu
  post_menu(*dir_menu);
  wrefresh(*dir_menu_win);

  /*
  attron(COLOR_PAIR(2));
  mvprintw(LINES - 9, 0, "SHIFT + A: Sort by file name");
  mvprintw(LINES - 8, 0, "SHIFT + S: Sort by file size");
  mvprintw(LINES - 7, 0, "SHIFT + D: Sort by modified date");
  mvprintw(LINES - 6, 0, "SHIFT + C: Copy file to clipboard");
  mvprintw(LINES - 5, 0, "SHIFT + V: Paste file in current directory");
  mvprintw(LINES - 4, 0, "DELETE   : Delete file");
  
  mvprintw(LINES - 2, 0, "Use PageUp and PageDown to scoll down or up a page of items");
  mvprintw(LINES - 1, 0, "Arrow Keys to navigate (F1 to Exit)");
  attroff(COLOR_PAIR(2));
  */
  refresh();
  
}

// Frees the contents of a filelist (file **)
void clear_filelist(file_info ** filelist){
  while ((*filelist) != NULL){
    
    free((*filelist)->name);
    (*filelist)->name = NULL;
    free((*filelist)->name_short);
    (*filelist)->name_short = NULL;
    free((*filelist)->type);
    (*filelist)->type = NULL;
    free((*filelist)->size);
    (*filelist)->size = NULL;
    free((*filelist)->description);
    (*filelist)->description = NULL;
    free(*filelist);
    *filelist = NULL;
    filelist++;
    
  }
}

// Frees the filelist (file_info **), and it's contents
void destroy_filelist(file_info ** filelist){
  file_info ** begin = filelist;
  clear_filelist(filelist);
  free(begin);

}

// Get next item beginning with letter (char c)
ITEM * get_lettered_item(ITEM ** menu_items, ITEM * current, int num_items, char c){

  int curr_index = item_index(current);
  
  int i = curr_index + 1;
  if (curr_index == 0) curr_index = num_items;
  while (i != curr_index){
    if (i == num_items) i = 0;
    if (item_name(menu_items[i])[0] == c || item_name(menu_items[i])[0] == ( c - 'a' + 'A' )) {
      return menu_items[i];
    }
    i++;    
  }
  return current;

}

// Refreshes the filelist for given directory
int build_filelist(file_info ** filelist, DIR * dfd){
  struct dirent * dp;
  struct stat stbuf;
  // Build array of file info and the menu
  clear_filelist(filelist);
  int item_no = 1;
  int last_item_no = 1;
  int i;

  int SHORTWIDTH = 21;
  //45
  SHORTWIDTH = MENUWIDTH - 55;
  SHORTWIDTH = SHORTWIDTH < 8 ? 8 : SHORTWIDTH;
  
  
  while ((dp = readdir(dfd))) {
    if (strcmp(dp->d_name, ".")){
      if (!strcmp(dp->d_name, "..")){
	last_item_no = item_no;
	item_no = 0;
      }
      
      stat(dp->d_name, &stbuf);
      int namelen = strlen(dp->d_name);
      if ((filelist[item_no] = calloc(1, sizeof(file_info))) == NULL){
        perror("calloc");
        exit(errno);
      }
      if ((filelist[item_no]->size = (char *) malloc(50)) == NULL){
        perror("malloc");
        exit(errno);
      }
      if ((filelist[item_no]->name = (char *) malloc(namelen + 1)) == NULL){
        perror("malloc");
        exit(errno);
      }
     
      if ((filelist[item_no]->type = (char *) malloc(10))== NULL){
        perror("malloc");
        exit(errno);
      }

      if ((filelist[item_no]->description = (char *) calloc(100, 1)) == NULL){
        perror("calloc");
        exit(errno);
      }

      filelist[item_no]->mod_date = (ctime(&stbuf.st_mtim.tv_sec));
      filelist[item_no]->mod_time = stbuf.st_mtim.tv_sec;
      
      memcpy(filelist[item_no]->name, dp->d_name, namelen + 1);
      filelist[item_no]->name_short = strndup(dp->d_name, SHORTWIDTH);
      
      if(namelen > SHORTWIDTH - 1){
	char * temp = &filelist[item_no]->name_short[SHORTWIDTH - 3];
	
	if (dp->d_name[namelen - 4] == '.') {
	  char * extension = &dp->d_name[namelen -3];
	  temp -= 1;
	  for (i = 0; i < 4; i++){
	    temp[i] = extension[i];
	  }
	  temp -= 3;
	}
	for (i = 0; i < 3; i++){
	  temp[i] = '.';
	}
      }
      
      sprintf(filelist[item_no]->size, "%.1fkb", ((float) stbuf.st_size) / 1024);
      filelist[item_no]->bytes = stbuf.st_size;
      
      filelist[item_no]->st_mode = stbuf.st_mode;
      if (S_ISREG(stbuf.st_mode)) {
	sprintf(filelist[item_no]->type, "FILE");
      } else if (S_ISDIR(stbuf.st_mode)) {
	sprintf(filelist[item_no]->type, " DIR");
      }
      
      sprintf(filelist[item_no]->description, "   %s%14s   %s", filelist[item_no]->type, filelist[item_no]->size, filelist[item_no]->mod_date);
      filelist[item_no]->description[strlen(filelist[item_no]->description) - 1] = 0;
      
      if (!strcmp(dp->d_name, "..")) item_no = last_item_no - 1;
      item_no++;	
    } 
  }
  closedir(dfd);
  return item_no;
  
}

// Print test to the bottom box
void refresh_littlebox(char * msg){
  move(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4);
  clrtoeol();
  // box for filename
  mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET, ACS_VLINE);
  mvaddch(MENUHEIGHT + Y_OFFSET , X_OFFSET, ACS_ULCORNER);
  mvaddch(MENUHEIGHT + Y_OFFSET + 2, X_OFFSET, ACS_LLCORNER);
  mvhline(MENUHEIGHT + Y_OFFSET + 2, X_OFFSET + 1, ACS_HLINE, MENUWIDTH - 2);
  mvhline(MENUHEIGHT + Y_OFFSET , X_OFFSET + 1, ACS_HLINE, MENUWIDTH - 2);
  mvaddch(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + MENUWIDTH - 1, ACS_VLINE);
  mvaddch(MENUHEIGHT + Y_OFFSET , X_OFFSET + MENUWIDTH - 1, ACS_URCORNER);
  mvaddch(MENUHEIGHT + Y_OFFSET +2, X_OFFSET + MENUWIDTH - 1, ACS_LRCORNER);
  
  mvprintw(MENUHEIGHT + Y_OFFSET + 1, X_OFFSET + 4, "%s", msg);
  refresh();
}

// Attempt to open file with a given program name
void run_prog(file_info * current_file_info, char * prog_name){
  int status;
  pid_t cpid;

  char * argstring = malloc(strlen(prog_name) + strlen(current_file_info->name) + 2);
  strcpy(argstring, prog_name);
  strcpy(&argstring[strlen(prog_name) + 1], current_file_info->name);
  

  int arg_count;
  char ** args = arg_parse(argstring, &arg_count);
  

/*
  char * args[3];
  args[0] = prog_name;
  args[1] = current_file_info->name;
  args[2] = NULL;
*/
  int fd[2];
  int bufflen = 80;
  char errorbuff[bufflen];
  if (pipe(fd) < 0){
    perror("pipe");
  }
  
  cpid = fork();
  
  if (cpid == 0){ //we are child
    close(fd[0]); // close read end of pipe
    execvp(args[0], args);
    write(fd[1], strerror(errno), bufflen);
    fclose(stdin);
    exit(127);
  }
  
  
  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }

  free(args);
  free(argstring);
  write(fd[1], "Success", bufflen);
  close(fd[1]);
  read(fd[0], errorbuff, bufflen);
  refresh_littlebox(errorbuff);
}

// Rename selected file
void rename_file(file_info * current_file_info){

  char  new_name[80];
  char * args[5];
  pid_t cpid;
  int status;
  
  int fd[2];
  int bufflen = 80;
  char errorbuff[bufflen];
  if (pipe(fd) < 0){
    perror("pipe");
  }
  
  args[0] = "mv";
  args[1] = "-f";
  args[2] = current_file_info->name;
  args[4] = NULL;
  refresh_littlebox("New Name: ");
  echo();
  curs_set(1);
  if (getstr(new_name) == ERR){
    curs_set(0);
    noecho();
    return;
  }
  curs_set(0);
  noecho();
  args[3] = new_name;

  cpid = fork();
  
  if (cpid == 0){ //we are child
    close(fd[0]);
    execvp(args[0], args);
    write(fd[1], strerror(errno), bufflen);
    //perror("exec");
    fclose(stdin);
    exit(127);
  }

  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }
  write(fd[1], "Success", bufflen);
  close(fd[1]);
  read(fd[0], errorbuff, bufflen);
  refresh_littlebox(errorbuff);
  sleep(1);

}

// Create new directory with mkdir()
int new_dir(){
  char  dir_name[80];
  
  refresh_littlebox("Directory Name: ");
  echo();
  curs_set(1);
  if (getstr(dir_name) == ERR){
    return -1;
    curs_set(0);
    noecho();
  }
  curs_set(0);
  noecho();
  
  if(mkdir(dir_name, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)){
    return -1;
  }
  return 0;
}

// Touch given filename (create new file)
void file_touch(){
  char touch_name[80];
  refresh_littlebox("Touch: ");
  echo();
  curs_set(1);
  getstr(touch_name);
  curs_set(0);
  noecho();

  char * args[3];
  args[0] = "touch";
  args[1] = touch_name;
  args[2] = NULL;
  pid_t cpid;
  int status;

  cpid = fork();
  
  if (cpid == 0){ //we are child
    execvp(args[0], args);
    
    perror("exec");
    fclose(stdin);
    exit(127);
  }

  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }
}

// Run bash
void open_terminal(char * dirbuff){
  int status;
  pid_t cpid;;

  //char * args[2];
  //args[0] = "/bin/bash";
  //args[1] = NULL;

  int fd[2];
  int bufflen = 80;
  char errorbuff[bufflen];
  if (pipe(fd) < 0){
    perror("pipe");
  }
  
  //ALLOW_INTERRUPT = 0;

  cpid = fork();

  
  
  if (cpid == 0){ //we are child
    close(fd[0]); // close read end of pipe
    //execvp(args[0], args);
    printf("\n=================================================\n");
    printf(" bash session - %s\n", dirbuff);
    printf(" Type 'exit' to return to gopher\n");
    printf("=================================================\n");
    execlp("/bin/bash", "bash", (char*) NULL);
    write(fd[1], strerror(errno), bufflen);
    fclose(stdin);
    exit(127);
  }
  
  
  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }
  //ALLOW_INTERRUPT = 1;
  write(fd[1], "Success", bufflen);
  close(fd[1]);
  read(fd[0], errorbuff, bufflen);
  refresh_littlebox(errorbuff);
}

// Signal handler for screen resize
void handle_winch(int sig){
  //fprintf(stderr, "Signal caught\n");
  MENUHEIGHT = LINES - 6;
  MENUWIDTH = COLS - 6;
  MENUWIDTH = MENUWIDTH > MENUWIDTH_MAX ? MENUWIDTH_MAX : MENUWIDTH;
  
  MENUHEIGHT = MENUHEIGHT > MENUHEIGHT_MAX ? MENUHEIGHT_MAX : MENUHEIGHT;
  if (!ALLOW_INTERRUPT) return;

  DIR * dfd;
  /*
  MENUHEIGHT = LINES - 6;
  MENUWIDTH = COLS - 6;
  MENUWIDTH = MENUWIDTH > MENUWIDTH_MAX ? MENUWIDTH_MAX : MENUWIDTH;
  
  MENUHEIGHT = MENUHEIGHT > MENUHEIGHT_MAX ? MENUHEIGHT_MAX : MENUHEIGHT;
  */
  endwin();
  refresh();
  if (!(dfd = opendir(sig_dirbuff))){
    fprintf(stderr, "Can't open directory\n");
  }
  *sig_nchoices = build_filelist(sig_filelist, dfd);
  sortfiles(sig_filelist, *sig_nchoices, comp_func);
  refresh_menu(sig_filelist, sig_menu_items, *sig_nchoices, sig_dir_menu, sig_dir_menu_win, sig_dirbuff);
}

// Exec a given command
void executecommand(char * msgbuff){
  ALLOW_INTERRUPT = 0;
  char command[200];
  refresh_littlebox("Execute Command: ");
  echo();
  curs_set(1);
  if (getstr(command) == ERR){
    
    refresh_littlebox("Whoops! Try again.");
    sleep(1);
    strcpy(msgbuff, "Whoops! Try again.");
    curs_set(0);
    noecho();
    ALLOW_INTERRUPT = 1;
    return;
  }
  curs_set(0);
  noecho();

  
  int status;
  pid_t cpid;;
  int fd[2];

  int bufflen = MSGWIDTH - 2;

  int arg_count;
  char ** args = arg_parse(command, &arg_count);

  if (pipe(fd) < 0){
    perror("pipe");
  }

  endwin();
  printf("\n==========================\n");
  printf("|| Executing Command... ||\n");
  printf("==========================\n");
  cpid = fork();

  
  if (cpid == 0){ //we are child
    close(fd[0]); // close read end of pipe

    execvp(args[0], args);

    fprintf(stdout, "exec: %s\n", strerror(errno));
    write(fd[1], strerror(errno), bufflen);
    fclose(stdin);
    exit(127);
  }
  
  
  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }
  printf("=================================================\n");
  printf("|| Finished. Press any key to return to gopher ||\n");
  printf("=================================================\n");
  cbreak();
  getch();
  free(args);
  write(fd[1], "Success", bufflen);
  close(fd[1]);
  
  read(fd[0], msgbuff, bufflen);
  
  refresh();
  ALLOW_INTERRUPT = 1;

}

void zip(file_info * current_file_info, char * msgbuff){
  pid_t cpid;
  int status;
  int fd[2];

  int bufflen = MSGWIDTH - 2;

  char archive_name[200];
  if (snprintf(archive_name, 200, "%s.zip", current_file_info->name) >= 200){
    sprintf(msgbuff, "Filename too long");
    return;
  }

  if (pipe(fd) < 0){
    perror("pipe");
  }
  ALLOW_INTERRUPT = 0;
  endwin();
  cpid = fork();
  if (cpid == 0){ //we are child
    close(fd[0]);
    execlp("zip", "zip", "-r", archive_name, current_file_info->name, NULL);
    write(fd[1], strerror(errno), bufflen);
    //perror("exec");
    fclose(stdin);
    exit(127);
  }

  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }
  ALLOW_INTERRUPT = 1;
  refresh();
  write(fd[1], "Success", bufflen);
  close(fd[1]);
  read(fd[0], msgbuff, bufflen);

}

void unzip(file_info * current_file_info, char * msgbuff){
  pid_t cpid;
  int status;
  int fd[2];

  int bufflen = MSGWIDTH - 2;

  if (pipe(fd) < 0){
    perror("pipe");
  }
  ALLOW_INTERRUPT = 0;
  endwin();
  cpid = fork();
  if (cpid == 0){ //we are child
    close(fd[0]);
    execlp("unzip", "unzip", "-u", current_file_info->name, NULL);
    write(fd[1], strerror(errno), bufflen);
    //perror("exec");
    fclose(stdin);
    exit(127);
  }

  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }
  
  refresh();
  write(fd[1], "Success", bufflen);
  close(fd[1]);
  read(fd[0], msgbuff, bufflen);
  ALLOW_INTERRUPT = 1;
}

void compress_tar(file_info * current_file_info, char * msgbuff){
  pid_t cpid;
  int status;
  int fd[2];

  int bufflen = MSGWIDTH - 2;

  char archive_name[200];
  if (snprintf(archive_name, 200, "%s.tar.gz", current_file_info->name) >= 200){
    sprintf(msgbuff, "Filename too long");
    return;
  }

  if (pipe(fd) < 0){
    perror("pipe");
  }
  ALLOW_INTERRUPT = 0;
  endwin();
  cpid = fork();
  if (cpid == 0){ //we are child
    close(fd[0]);
    execlp("tar", "tar", "-czvf", archive_name, current_file_info->name, NULL);
    write(fd[1], strerror(errno), bufflen);
    //perror("exec");
    fclose(stdin);
    exit(127);
  }

  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }
  
  refresh();
  write(fd[1], "Success", bufflen);
  close(fd[1]);
  read(fd[0], msgbuff, bufflen);
  ALLOW_INTERRUPT = 1;
}

void extract_tar(file_info * current_file_info, char * msgbuff){
  pid_t cpid;
  int status;
  int fd[2];

  int bufflen = MSGWIDTH - 2;

  if (pipe(fd) < 0){
    perror("pipe");
  }
  ALLOW_INTERRUPT = 0;
  endwin();
  cpid = fork();
  if (cpid == 0){ //we are child
    close(fd[0]);
    execlp("tar", "tar", "-xzvf", current_file_info->name, NULL);
    write(fd[1], strerror(errno), bufflen);
    //perror("exec");
    fclose(stdin);
    exit(127);
  }

  while (waitpid(cpid, &status, 0) < 0){
    perror("wait");
  }
  
  refresh();
  write(fd[1], "Success", bufflen);
  close(fd[1]);
  read(fd[0], msgbuff, bufflen);
  ALLOW_INTERRUPT = 1;
}