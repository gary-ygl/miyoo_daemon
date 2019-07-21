/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <linux/ioctl.h>
#include <sys/fcntl.h> 
#include <sys/stat.h>
#include <sys/ioctl.h>

#define MIYOO_VIR_SET_MODE    _IOWR(0x100, 0, unsigned long)
#define MIYOO_VIR_SET_VER     _IOWR(0x101, 0, unsigned long)
#define MIYOO_SND_SET_VOLUME  _IOWR(0x100, 0, unsigned long)
#define MIYOO_KBD_GET_HOTKEY  _IOWR(0x100, 0, unsigned long)
#define MIYOO_KBD_SET_VER     _IOWR(0x101, 0, unsigned long)
#define MIYOO_FB0_PUT_OSD     _IOWR(0x100, 0, unsigned long)
#define MIYOO_FB0_SET_MODE    _IOWR(0x101, 0, unsigned long)
#define MIYOO_FB0_GET_VER     _IOWR(0x102, 0, unsigned long)
#define MIYOO_FB0_SET_FPBP    _IOWR(0x104, 0, unsigned long)
#define MIYOO_FB0_GET_FPBP    _IOWR(0x105, 0, unsigned long)

#define MIYOO_FBP_FILE        "/mnt/.fpbp.conf"
#define MIYOO_LID_FILE        "/mnt/.backlight.conf"
#define MIYOO_VOL_FILE        "/mnt/.volume.conf"
#define MIYOO_LID_CONF        "/sys/devices/platform/backlight/backlight/backlight/brightness"
#define MIYOO_BUTTON_FILE     "/mnt/.buttons.conf"

static void create_daemon(void)
{
  int x;

  pid_t pid;

  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }

  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  if (setsid() < 0) {
    exit(EXIT_FAILURE);
  }
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }

  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }
  umask(0);
  chdir("/");
  
  for (x = sysconf(_SC_OPEN_MAX); x>=0; x--){
    close(x);
  }
}

static int read_conf(const char *file)
{
  int i, fd;
  int val = 5;
  char buf[10]={0};
  
  fd = open(file, O_RDWR);
  if(fd < 0){
    val = -1;
  }
  else{
    read(fd, buf, sizeof(buf));
    for(i=0; i<strlen(buf); i++){
      if(buf[i] == '\r'){
        buf[i] = 0;
      }
      if(buf[i] == '\n'){
        buf[i] = 0;
      }
      if(buf[i] == ' '){
        buf[i] = 0;
      }
    }
    val = atoi(buf);
  }
  close(fd);
  return val;
}

static void write_conf(const char *file, int val)
{
  int fd;
  char buf[10]={0};
  
  fd = open(file, O_WRONLY | O_CREAT | O_TRUNC);
  if(fd > 0){
    sprintf(buf, "%d", val);
    write(fd, buf, strlen(buf));
    close(fd);
  }
}

char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        *(result + idx) = 0;
    }

    return result;
}

static void read_button_config(const char *file, char *vals)
{
  
  int i, fd;
  char buf[20]={0};
  
  fd = open(file, O_RDWR);
  if(fd < 0){
    return;
  }
  else{
    read(fd, buf, sizeof(buf));
    for(i=0; i<strlen(buf); i++){
      if(buf[i] == '\r'){
        buf[i] = 0;
      }
      if(buf[i] == '\n'){
        buf[i] = 0;
      }
      if(buf[i] == ' '){
        buf[i] = 0;
      }
    }
    char** tokens;
    tokens = str_split(buf, ':');
    for(i=0; i<4; i++){
      vals[i] = atol(tokens[i]);
    }
    free(tokens);
  }
  close(fd);
  return;
}

static void info_fb0(int fb0, int lid, int vol, int show_osd)
{
  unsigned long val;

  val = (show_osd ? 0x80000000 : 0x00000000) | (vol << 16) | (lid);
  ioctl(fb0, MIYOO_FB0_PUT_OSD, val);
}

int main(int argc, char** argv)
{
  int lid=0, vol=0, fbp=0;
  char buf[255]={0};
  unsigned long ret, lastret;
  int fb0, kbd, snd, vir;
  unsigned char actionmap[4]={1,2,3,4};

  create_daemon();
  fb0 = open("/dev/miyoo_fb0", O_RDWR);
  kbd = open("/dev/miyoo_kbd", O_RDWR);
  snd = open("/dev/miyoo_snd", O_RDWR);

  // fp, bp
  fbp = read_conf(MIYOO_FBP_FILE);
  if(fbp > 0){
    ioctl(fb0, MIYOO_FB0_SET_FPBP, fbp);
  }

  // backlight
  lid = read_conf(MIYOO_LID_FILE);
  if(lid < 0){
    lid = 5;
    write_conf(MIYOO_LID_FILE, lid);
  }
  sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF);
  system(buf);
  
  // volume
  vol = read_conf(MIYOO_VOL_FILE);
  if(vol < 0){
    vol = 5;
    write_conf(MIYOO_VOL_FILE, vol);
  }
  ioctl(snd, MIYOO_SND_SET_VOLUME, vol);

  // buttons
  read_button_config(MIYOO_BUTTON_FILE,actionmap);

  // info fb0
  info_fb0(fb0, lid, vol, 0);

  // update version
  ioctl(fb0, MIYOO_FB0_GET_VER, &ret);
  ioctl(kbd, MIYOO_KBD_SET_VER, ret);
  vir = open("/dev/miyoo_vir", O_RDWR);
  ioctl(vir, MIYOO_VIR_SET_VER, ret);
  close(vir);
  lastret = 0;
  while(1){
    usleep(100000);
    ioctl(kbd, MIYOO_KBD_GET_HOTKEY, &ret);
    if(ret == 0 && lastret == 0){
      continue;
    } else if(ret == 0 && lastret != 0) {
      switch(actionmap[lastret-1]){
      case 1:
        //printf("backlight++\n");
        if(lid < 10){
          lid+= 1;
          write_conf(MIYOO_LID_FILE, lid);
          sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF);
          system(buf);
          info_fb0(fb0, lid, vol, 1);
        }
        break;
      case 2:
        //printf("backlight--\n");
        if(lid > 1){
          lid-= 1;
          write_conf(MIYOO_LID_FILE, lid);
          sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF);
          system(buf);
          info_fb0(fb0, lid, vol, 1);
        }
        break;
      case 3:
        //printf("sound++\n");
        if(vol < 9){
          vol+= 1;
          write_conf(MIYOO_VOL_FILE, vol);
          ioctl(snd, MIYOO_SND_SET_VOLUME, vol);
          info_fb0(fb0, lid, vol, 1);
        }
        break;
      case 4:
        //printf("sound--\n");
        if(vol > 0){
          vol-= 1;
          write_conf(MIYOO_VOL_FILE, vol);
          ioctl(snd, MIYOO_SND_SET_VOLUME, vol);
          info_fb0(fb0, lid, vol, 1);
        }
        break;
      case 5:
        //printf("mute\n");
        if(vol == 0){
          vol = read_conf(MIYOO_VOL_FILE);
          if(vol < 1){
            vol = 5;
            write_conf(MIYOO_VOL_FILE, vol);
          }
          ioctl(snd, MIYOO_SND_SET_VOLUME, vol);
        } else {
          vol = 0;
          ioctl(snd, MIYOO_SND_SET_VOLUME, vol);
        }
        break;
      case 6: 
      //printf("volume rotate up\n"); 
        if(vol < 9){ 
          vol+= 1;
          write_conf(MIYOO_VOL_FILE, vol);
          ioctl(snd, MIYOO_SND_SET_VOLUME, vol);
          info_fb0(fb0, lid, vol, 1);
        } else { 
          vol = 0;
          write_conf(MIYOO_VOL_FILE, vol);
          ioctl(snd, MIYOO_SND_SET_VOLUME, vol);
          info_fb0(fb0, lid, vol, 1);
        } 
      case 7: 
      //printf("volume rotate down\n"); 
        if(vol < 1){ 
          vol = 9;
          write_conf(MIYOO_VOL_FILE, vol);
          ioctl(snd, MIYOO_SND_SET_VOLUME, vol);
          info_fb0(fb0, lid, vol, 1);
        } else { 
          vol -= 1;
          write_conf(MIYOO_VOL_FILE, vol);
          ioctl(snd, MIYOO_SND_SET_VOLUME, vol);
          info_fb0(fb0, lid, vol, 1);
        } 
      case 8: 
      //printf("backlight rotate up\n"); 
        if(lid < 10){ 
          lid+= 1; 
          write_conf(MIYOO_LID_FILE, lid); 
          sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF); 
          system(buf); 
          info_fb0(fb0, lid, vol, 1); 
        } else { 
          lid= 1; 
          write_conf(MIYOO_LID_FILE, lid); 
          sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF); 
          system(buf); 
          info_fb0(fb0, lid, vol, 1); 
        } 
      break;
      case 9: 
      //printf("backlight rotate down\n"); 
        if(lid == 1){ 
          lid = 10; 
          write_conf(MIYOO_LID_FILE, lid); 
          sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF); 
          system(buf); 
          info_fb0(fb0, lid, vol, 1); 
        } else { 
          lid -= 1; 
          write_conf(MIYOO_LID_FILE, lid); 
          sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF); 
          system(buf); 
          info_fb0(fb0, lid, vol, 1); 
        } 
      break;
      case 10: 
      //printf("backlight min max\n"); 
        if(lid != 10){ 
          lid = 10; 
          write_conf(MIYOO_LID_FILE, lid); 
          sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF); 
          system(buf); 
          info_fb0(fb0, lid, vol, 1); 
        } else { 
          lid = 2; 
          write_conf(MIYOO_LID_FILE, lid); 
          sprintf(buf, "echo %d > %s", lid, MIYOO_LID_CONF); 
          system(buf); 
          info_fb0(fb0, lid, vol, 1); 
        } 
      break;
      case 20:
        {
          int status;
          pid_t son = fork();
          if (!son) {
            execlp("sh", "sh", "-c", "kill $(ps -al | grep \"/mnt/\" | grep -v \"/kernel/\" | tr -s [:blank:] | cut -d \" \" -f 2) ; sleep 0.1 ; sync && poweroff",  NULL);
          }
          break;
        }
      case 21:
        {
          int status;
          pid_t son = fork();
          if (!son) {
            //execlp("sh", "sh", "/mnt/kernel/killgui.sh", NULL);
            execlp("sh", "sh", "-c", "kill $(ps -al | grep \"/mnt/\" | grep -v \"/kernel/\" | tr -s [:blank:] | cut -d \" \" -f 2)",  NULL);
          }
          break;
        }
      }
    } 
    lastret = ret;
  }
 
  close(fb0);
  close(kbd);
  close(snd);
  return EXIT_SUCCESS;
}

