/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "input/evdev.h"
#include "config.h"
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#define MOONLIGHT_PATH "/moonlight/"
#define USER_PATHS ":~/.moonlight/:./"

#define write_config_string(fd, key, value) fprintf(fd, "%s = %s\n", key, value)
#define write_config_int(fd, key, value) fprintf(fd, "%s = %d\n", key, value)
#define write_config_bool(fd, key, value) fprintf(fd, "%s = %s\n", key, value?"true":"false");

bool inputAdded = false;
static bool mapped = true;

static struct option long_options[] = {
  {"720", no_argument, NULL, 'a'},
  {"1080", no_argument, NULL, 'b'},
  {"width", required_argument, NULL, 'c'},
  {"height", required_argument, NULL, 'd'},
  {"30fps", no_argument, NULL, 'e'},
  {"60fps", no_argument, NULL, 'f'},
  {"bitrate", required_argument, NULL, 'g'},
  {"packetsize", required_argument, NULL, 'h'},
  {"app", required_argument, NULL, 'i'},
  {"input", required_argument, NULL, 'j'},
  {"mapping", required_argument, NULL, 'k'},
  {"nosops", no_argument, NULL, 'l'},
  {"audio", required_argument, NULL, 'm'},
  {"localaudio", no_argument, NULL, 'n'},
  {"config", required_argument, NULL, 'o'},
  {"platform", required_argument, 0, 'p'},
  {"save", required_argument, NULL, 'q'},
  {0, 0, 0, 0},
};

char* get_path(char* name) {
  const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
  char *data_dirs;

  if (access(name, R_OK) != -1) {
      return name;
  }

  if (!xdg_data_dirs)
    data_dirs = "/usr/share:/usr/local/share:" USER_PATHS;
  else {
    data_dirs = malloc(strlen(xdg_data_dirs) + strlen(USER_PATHS) + 1);
    strcpy(data_dirs, xdg_data_dirs);
    strcpy(data_dirs+strlen(data_dirs), USER_PATHS);
  }

  char *path = malloc(strlen(data_dirs)+strlen(MOONLIGHT_PATH)+strlen(name)+1);
  if (path == NULL) {
    fprintf(stderr, "Not enough memory\n");
    exit(-1);
  }

  char* end;
  do {
    end = strstr(data_dirs, ":");
    int length = end != NULL?end - data_dirs:strlen(data_dirs);
    memcpy(path, data_dirs, length);
    if (path[0] == '/')
      sprintf(path+length, "%s%s", MOONLIGHT_PATH, name);
    else
      sprintf(path+length, "%s", name);

    if(access(path, R_OK) != -1)
      return path;

    data_dirs = end + 1;
  } while (end != NULL);

  free(path);
  return NULL;
}

static void parse_argument(int c, char* value, PCONFIGURATION config) {
  switch (c) {
  case 'a':
    config->stream.width = 1280;
    config->stream.height = 720;
    break;
  case 'b':
    config->stream.width = 1920;
    config->stream.height = 1080;
    break;
  case 'c':
    config->stream.width = atoi(value);
    break;
  case 'd':
    config->stream.height = atoi(value);
    break;
  case 'e':
    config->stream.fps = 30;
    break;
  case 'f':
    config->stream.fps = 60;
    break;
  case 'g':
    config->stream.bitrate = atoi(value);
    break;
  case 'h':
    config->stream.packetSize = atoi(value);
    break;
  case 'i':
    config->app = value;
    break;
  case 'j':
    if (config->inputsCount >= MAX_INPUTS) {
      perror("Too many inputs specified");
      exit(-1);
    }
    config->inputs[config->inputsCount].path = value;
    config->inputs[config->inputsCount].mapping = value;
    config->inputsCount++;
    inputAdded = true;
    mapped = true;
    break;
  case 'k':
    config->mapping = get_path(value);
    if (config->mapping == NULL) {
      fprintf(stderr, "Unable to open custom mapping file: %s\n", value);
      exit(-1);
    }
    mapped = false;
    break;
  case 'l':
    config->sops = false;
    break;
  case 'm':
    audio_device = value;
    break;
  case 'n':
    config->localaudio = true;
    break;
  case 'o':
    config_file_parse(value, config);
    break;
  case 'p':
    config->platform = value;
    break;
  case 'q':
    config->config_file = value;
    break;
  case 1:
    if (config->action == NULL)
      config->action = value;
    else if (config->address == NULL)
      config->address = value;
    else {
      perror("Too many options");
      exit(-1);
    }
  }
}

void config_file_parse(char* filename, PCONFIGURATION config) {
  FILE* fd = fopen(filename, "r");
  if (fd == NULL) {
    fprintf(stderr, "Can't open configuration file: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  char *line = NULL;
  size_t len = 0;

  while (getline(&line, &len, fd) != -1) {
    char *key = NULL, *value = NULL;
    if (sscanf(line, "%ms = %ms", &key, &value) == 2) {
      if (strcmp(key, "address") == 0) {
        config->address = value;
      } else {
        for (int i=0;long_options[i].name != NULL;i++) {
          if (long_options[i].has_arg = required_argument && strcmp(long_options[i].name, key) == 0) {
            parse_argument(long_options[i].val, value, config);
          }
        }
      }
    }
  }
}

void config_save(char* filename, PCONFIGURATION config) {
  FILE* fd = fopen(filename, "w");
  if (fd == NULL) {
    fprintf(stderr, "Can't open configuration file: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  if (config->stream.width != 1280)
    write_config_int(fd, "width", config->stream.width);
  if (config->stream.height != 720)
    write_config_int(fd, "height", config->stream.height);
  if (config->stream.fps != 60)
    write_config_int(fd, "fps", config->stream.fps);
  if (config->stream.bitrate != -1)
    write_config_int(fd, "bitrate", config->stream.bitrate);
  if (config->stream.packetSize != 1024)
    write_config_int(fd, "packetsize", config->stream.packetSize);
  if (!config->sops)
    write_config_bool(fd, "sops", config->sops);
  if (config->localaudio)
    write_config_bool(fd, "localaudio", config->localaudio);

  if (strcmp(config->app, "Steam") != 0)
    write_config_string(fd, "app", config->app);

  fclose(fd);
}

void config_parse(int argc, char* argv[], PCONFIGURATION config) {
  config->stream.width = 1280;
  config->stream.height = 720;
  config->stream.fps = 60;
  config->stream.bitrate = -1;
  config->stream.packetSize = 1024;

  config->platform = "default";
  config->app = "Steam";
  config->action = NULL;
  config->address = NULL;
  config->config_file = NULL;
  config->sops = true;
  config->localaudio = false;

  config->inputsCount = 0;
  config->mapping = get_path("mappings/default.conf");

  if (argc == 2 && access(argv[1], F_OK) == 0) {
    config->action = "stream";
    config_file_parse(argv[1], config);
  } else {
    int option_index = 0;
    int c;
    while ((c = getopt_long_only(argc, argv, "-abc:d:efg:h:i:j:k:lm:no:p:q:", long_options, &option_index)) != -1) {
      parse_argument(c, optarg, config);
    }
  }

  if (config->config_file != NULL)
    config_save(config->config_file, config);

  if (config->stream.bitrate == -1) {
    if (config->stream.height >= 1080 && config->stream.fps >= 60)
      config->stream.bitrate = 20000;
    else if (config->stream.height >= 1080 || config->stream.fps >= 60)
      config->stream.bitrate = 10000;
    else
      config->stream.bitrate = 5000;
  }

  if (inputAdded && !mapped) {
    fprintf(stderr, "Mapping option should be followed by the input to be mapped.\n");
    exit(-1);
  }
}
