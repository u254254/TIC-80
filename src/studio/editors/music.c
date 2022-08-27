// MIT License

// Copyright (c) 2017 Vadim Grigoruk @nesbox // grigoruk@gmail.com

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "music.h"
#include "ext/history.h"

#include <ctype.h>

#define TRACKER_ROWS (MUSIC_PATTERN_ROWS / 4)
#define CHANNEL_COLS 8
#define TRACKER_COLS (TIC_SOUND_CHANNELS * CHANNEL_COLS)

#define CMD_STRING(_, c, ...) DEF2STR(c)
static const char MusicCommands[] = MUSIC_CMD_LIST(CMD_STRING);
#undef CMD_STRING

enum
{
    ColumnNote = 0,
    ColumnSemitone,
    ColumnOctave,
    ColumnSfxHi,
    ColumnSfxLow,
    ColumnCommand,
    ColumnParameter1,
    ColumnParameter2,
};

static void undo(Music* music)
{
    history_undo(music->history);
}

static void redo(Music* music)
{
    history_redo(music->history);
}

static const tic_music_state* getMusicPos(Music* music)
{
    return &music->tic->ram->music_state;
}

static void drawEditPanel(Music* music, s32 x, s32 y, s32 w, s32 h)
{
    tic_mem* tic = music->tic;

    tic_api_rect(tic, x, y-1, w, 1, tic_color_dark_grey);
    tic_api_rect(tic, x-1, y, 1, h, tic_color_dark_grey);
    tic_api_rect(tic, x, y+h, w, 1, tic_color_light_grey);
    tic_api_rect(tic, x+w, y, 1, h, tic_color_light_grey);

    tic_api_rect(tic, x, y, w, h, tic_color_black);
}

static void setChannelPattern(Music* music, s32 delta, s32 channel);

static s32 getStep(Music* music)
{
    enum{DefaultStep = 1, ExtraStep = 10};

    return tic_api_key(music->tic, tic_key_shift) ? ExtraStep : DefaultStep;
}

static void drawEditbox(Music* music, s32 x, s32 y, s32 value, void(*set)(Music*, s32, s32 channel), s32 channel)
{
    tic_mem* tic = music->tic;

    {
        tic_rect rect = { x - TIC_FONT_WIDTH, y, TIC_ALTFONT_WIDTH, TIC_FONT_HEIGHT };

        bool over = false;
        bool down = false;
        if (checkMousePos(music->studio, &rect))
        {
            setCursor(music->studio, tic_cursor_hand);
            over = true;

            if (checkMouseDown(music->studio, &rect, tic_mouse_left))
                down = true;

            if (checkMouseClick(music->studio, &rect, tic_mouse_left))
                set(music, -1, channel);
        }

        drawBitIcon(music->studio, tic_icon_left, rect.x - 2, rect.y + (down ? 1 : 0), tic_color_black);
        drawBitIcon(music->studio, tic_icon_left, rect.x - 2, rect.y + (down ? 0 : -1), (over ? tic_color_light_grey : tic_color_dark_grey));
    }

    {
        tic_rect rect = { x-1, y-1, TIC_FONT_WIDTH*2+1, TIC_FONT_HEIGHT+1 };

        if (checkMousePos(music->studio, &rect))
        {
            setCursor(music->studio, tic_cursor_hand);

            showTooltip(music->studio, "select pattern ID");

            bool left = checkMouseClick(music->studio, &rect, tic_mouse_left);
            if(left || checkMouseClick(music->studio, &rect, tic_mouse_right))
            {
                tic_point pos = {channel * CHANNEL_COLS, -1};

                if(MEMCMP(music->tracker.edit, pos))
                {
                    s32 step = getStep(music);
                    setChannelPattern(music, left ? +step : -step, channel);
                }
                else
                {
                    music->tracker.edit = pos;
                    s32 mx = tic_api_mouse(tic).x - rect.x;
                    music->tracker.col = mx / TIC_FONT_WIDTH ? 1 : 0;
                }
            }
        }

        drawEditPanel(music, rect.x, rect.y, rect.w, rect.h);

        if(music->tracker.edit.y == -1 && music->tracker.edit.x / CHANNEL_COLS == channel)
        {
            tic_api_rect(music->tic, x - 1 + music->tracker.col * TIC_FONT_WIDTH, y - 1, TIC_FONT_WIDTH + 1, TIC_FONT_HEIGHT + 1, tic_color_red);
        }

        char val[] = "99";
        sprintf(val, "%02i", value);
        tic_api_print(music->tic, val, x, y, tic_color_white, true, 1, false);
    }

    {
        tic_rect rect = { x + TIC_FONT_WIDTH*2+1, y, TIC_ALTFONT_WIDTH, TIC_FONT_HEIGHT };

        bool over = false;
        bool down = false;
        if (checkMousePos(music->studio, &rect))
        {
            setCursor(music->studio, tic_cursor_hand);
            over = true;

            if (checkMouseDown(music->studio, &rect, tic_mouse_left))
                down = true;

            if (checkMouseClick(music->studio, &rect, tic_mouse_left))
                set(music, +1, channel);
        }

        drawBitIcon(music->studio, tic_icon_right, rect.x - 1, rect.y + (down ? 1 : 0), tic_color_black);
        drawBitIcon(music->studio, tic_icon_right, rect.x - 1, rect.y + (down ? 0 : -1), (over ? tic_color_light_grey : tic_color_dark_grey));
    }
}

static inline s32 switchWidth(s32 value)
{
    return value > 99 ? 3 * TIC_FONT_WIDTH : 2 * TIC_FONT_WIDTH;
}

static void drawSwitch(Music* music, s32 x, s32 y, const char* label, s32 value, void(*set)(Music*, s32))
{
    tic_mem* tic = music->tic;

    tic_api_print(tic, label, x, y+1, tic_color_black, true, 1, false);
    tic_api_print(tic, label, x, y, tic_color_white, true, 1, false);

    x += strlen(label) * TIC_FONT_WIDTH + TIC_ALTFONT_WIDTH;

    {
        tic_rect rect = { x - TIC_ALTFONT_WIDTH, y, TIC_ALTFONT_WIDTH, TIC_FONT_HEIGHT };

        bool over = false;
        bool down = false;
        if (checkMousePos(music->studio, &rect))
        {
            setCursor(music->studio, tic_cursor_hand);

            over = true;

            if (checkMouseDown(music->studio, &rect, tic_mouse_left))
                down = true;

            if (checkMouseClick(music->studio, &rect, tic_mouse_left))
                set(music, value - getStep(music));
        }

        drawBitIcon(music->studio, tic_icon_left, rect.x - 2, rect.y + (down ? 1 : 0), tic_color_black);
        drawBitIcon(music->studio, tic_icon_left, rect.x - 2, rect.y + (down ? 0 : -1), over ? tic_color_light_grey : tic_color_dark_grey);
    }

    {
        tic_rect rect = { x, y, switchWidth(value), TIC_FONT_HEIGHT };

        if((tic->ram->input.mouse.btns & 1) == 0)
            music->drag.label = NULL;

        if(music->drag.label == label)
        {
            tic_point m = tic_api_mouse(tic);
            enum{Speed = 2};
            set(music, music->drag.start + (m.x - music->drag.x) / Speed);
        }

        if (checkMousePos(music->studio, &rect))
        {
            setCursor(music->studio, tic_cursor_hand);

            if (checkMouseDown(music->studio, &rect, tic_mouse_left))
            {
                if(!music->drag.label)
                {
                    music->drag.label = label;
                    music->drag.x = tic_api_mouse(tic).x;
                    music->drag.start = value;
                }
            }

            bool left = checkMouseClick(music->studio, &rect, tic_mouse_left);
            if(left || checkMouseClick(music->studio, &rect, tic_mouse_right))
            {
                s32 step = getStep(music);
                set(music, value + (left ? +step : -step));
            }
        }

        char val[sizeof "999"];
        sprintf(val, "%02i", value);
        tic_api_print(tic, val, x, y+1, tic_color_black, true, 1, false);
        tic_api_print(tic, val, x, y, tic_color_yellow, true, 1, false);
    }

    {
        tic_rect rect = { x + switchWidth(value), y, TIC_ALTFONT_WIDTH, TIC_FONT_HEIGHT };

        bool over = false;
        bool down = false;
        if (checkMousePos(music->studio, &rect))
        {
            setCursor(music->studio, tic_cursor_hand);

            over = true;

            if (checkMouseDown(music->studio, &rect, tic_mouse_left))
                down = true;

            if (checkMouseClick(music->studio, &rect, tic_mouse_left))
                set(music, value + getStep(music));
        }

        drawBitIcon(music->studio, tic_icon_right, rect.x - 2, rect.y + (down ? 1 : 0), tic_color_black);
        drawBitIcon(music->studio, tic_icon_right, rect.x - 2, rect.y + (down ? 0 : -1), over ? tic_color_light_grey : tic_color_dark_grey);
    }
}

static tic_track* getTrack(Music* music)
{
    return &music->src->tracks.data[music->track];
}

static s32 getRows(Music* music)
{
    tic_track* track = getTrack(music);

    return MUSIC_PATTERN_ROWS - track->rows;
}

static void updateScroll(Music* music)
{
    music->scroll.pos = CLAMP(music->scroll.pos, 0, getRows(music) - TRACKER_ROWS);
}

static void updateTracker(Music* music)
{
    s32 row = music->tracker.edit.y;

    enum{Threshold = TRACKER_ROWS / 2};
    music->scroll.pos = CLAMP(music->scroll.pos, row - (TRACKER_ROWS - Threshold), row - Threshold);

    {
        s32 rows = getRows(music);
        if (music->tracker.edit.y >= rows) music->tracker.edit.y = rows - 1;
    }

    updateScroll(music);
}

static void upRow(Music* music)
{
    if (music->tracker.edit.y > -1)
    {
        music->tracker.edit.y--;
        updateTracker(music);
    }
}

static void downRow(Music* music)
{
    const tic_music_state* pos = getMusicPos(music);
    // Don't move the cursor if the track is being played/recorded
    if(pos->music.track == music->track && music->follow) return;

    if (music->tracker.edit.y < getRows(music) - 1)
    {
        music->tracker.edit.y++;
        updateTracker(music);
    }
}

static void leftCol(Music* music)
{
    if (music->tracker.edit.x > 0)
    {
        // skip sharp column
        if(music->tracker.edit.x == 2) {
            music->tracker.edit.x--;
        }
        music->tracker.edit.x--;
        updateTracker(music);
    }
}

static void rightCol(Music* music)
{
    if (music->tracker.edit.x < TRACKER_COLS - 1)
    {
        // skip sharp column
        if(music->tracker.edit.x == 0) {
            music->tracker.edit.x++;
        }
        music->tracker.edit.x++;
        updateTracker(music);
    }
}

static void goHome(Music* music)
{
    music->tracker.edit.x -= music->tracker.edit.x % CHANNEL_COLS;
}

static void goEnd(Music* music)
{
    music->tracker.edit.x -= music->tracker.edit.x % CHANNEL_COLS;
    music->tracker.edit.x += CHANNEL_COLS-1;
}

static void pageUp(Music* music)
{
    music->tracker.edit.y -= TRACKER_ROWS;
    if(music->tracker.edit.y < 0) 
        music->tracker.edit.y = 0;

    updateTracker(music);
}

static void pageDown(Music* music)
{
    if (music->tracker.edit.y < getRows(music) - 1)

    music->tracker.edit.y += TRACKER_ROWS;
    s32 rows = getRows(music);

    if(music->tracker.edit.y >= rows) 
        music->tracker.edit.y = rows-1;
    
    updateTracker(music);
}

static void doTab(Music* music)
{
    tic_mem* tic = music->tic;
    s32 inc = tic_api_key(tic, tic_key_shift) ? -1 : +1;

    s32 channel = (music->tracker.edit.x / CHANNEL_COLS + TIC_SOUND_CHANNELS + inc) % TIC_SOUND_CHANNELS;
    music->tracker.edit.x = channel * CHANNEL_COLS + music->tracker.edit.x % CHANNEL_COLS;

    updateTracker(music);
}

static void upFrame(Music* music)
{
    music->frame--;

    if(music->frame < 0)
        music->frame = 0;
}

static void downFrame(Music* music)
{
    music->frame++;

    if(music->frame >= MUSIC_FRAMES)
        music->frame = MUSIC_FRAMES-1;
}

static bool checkPlaying(Music* music)
{
    return getMusicPos(music)->music.track >= 0;
}

static bool checkPlayFrame(Music* music, s32 frame)
{
    const tic_music_state* pos = getMusicPos(music);

    return pos->music.track == music->track &&
        pos->music.frame == frame;
}

static bool checkPlayRow(Music* music, s32 row)
{
    const tic_music_state* pos = getMusicPos(music);

    return checkPlayFrame(music, music->frame) && pos->music.row == row;
}

static tic_track_pattern* getFramePattern(Music* music, s32 channel, s32 frame)
{
    s32 patternId = tic_tool_get_pattern_id(getTrack(music), frame, channel);

    return patternId ? &music->src->patterns.data[patternId - PATTERN_START] : NULL;
}

static tic_track_pattern* getPattern(Music* music, s32 channel)
{
    return getFramePattern(music, channel, music->frame);
}

static tic_track_pattern* getChannelPattern(Music* music)
{
    s32 channel = music->tracker.edit.x / CHANNEL_COLS;

    return getPattern(music, channel);
}

static s32 getNote(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    return pattern->rows[music->tracker.edit.y].note - NoteStart;
}

static s32 getOctave(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    return pattern->rows[music->tracker.edit.y].octave;
}

static s32 getSfx(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    return tic_tool_get_track_row_sfx(&pattern->rows[music->tracker.edit.y]);
}

static inline tic_music_status getMusicState(Music* music)
{
    return music->tic->ram->music_state.flag.music_status;
}

static inline void setMusicState(Music* music, tic_music_status state)
{
    music->tic->ram->music_state.flag.music_status = state;
}

static void playNote(Music* music, const tic_track_row* row)
{
    tic_mem* tic = music->tic;

    if(getMusicState(music) == tic_music_stop && row->note >= NoteStart)
    {
        s32 channel = music->piano.col;
        sfx_stop(tic, channel);
        tic_api_sfx(tic, tic_tool_get_track_row_sfx(row), row->note - NoteStart, row->octave, TIC80_FRAMERATE / 4, channel, MAX_VOLUME, MAX_VOLUME, 0);
    }
}

static void setSfx(Music* music, s32 sfx)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];

    tic_tool_set_track_row_sfx(row, sfx);

    music->last.sfx = tic_tool_get_track_row_sfx(&pattern->rows[music->tracker.edit.y]);

    playNote(music, row);
}

static void setStopNote(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    pattern->rows[music->tracker.edit.y].note = NoteStop;
    pattern->rows[music->tracker.edit.y].octave = 0;
}

static void setNote(Music* music, s32 note, s32 octave, s32 sfx)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];
    row->note = note + NoteStart;
    row->octave = octave;
    tic_tool_set_track_row_sfx(row, sfx);

    playNote(music, row);
}

static void setOctave(Music* music, s32 octave)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    tic_track_row* row = &pattern->rows[music->tracker.edit.y];
    row->octave = octave;

    music->last.octave = octave;

    playNote(music, row);
}

static void setCommandDefaults(tic_track_row* row)
{
    switch(row->command)
    {
    case tic_music_cmd_volume:
        row->param2 = row->param1 = MAX_VOLUME;
        break;
    case tic_music_cmd_pitch:
        row->param1 = PITCH_DELTA >> 4;
        row->param2 = PITCH_DELTA & 0xf;
    default: break;
    }
}

static void setCommand(Music* music, tic_music_command command)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];

    tic_music_command prev = row->command;
    row->command = command;

    if(prev == tic_music_cmd_empty)
        setCommandDefaults(row);
}

static void setParam1(Music* music, u8 value)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];

    row->param1 = value;
}

static void setParam2(Music* music, u8 value)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    tic_track_row* row = &pattern->rows[music->tracker.edit.y];

    row->param2 = value;
}

static void playTrackFromNow(Music* music)
{
    tic_mem* tic = music->tic;

    tic_api_music(tic, music->track, music->frame, music->tracker.edit.y, music->loop, music->sustain, -1, -1);
}

static void playFrame(Music* music)
{
    tic_mem* tic = music->tic;

    tic_api_music(tic, music->track, music->frame, -1, music->loop, music->sustain, -1, -1);

    setMusicState(music, tic_music_play_frame);
}

static void playTrack(Music* music)
{
    tic_api_music(music->tic, music->track, -1, -1, music->loop, music->sustain, -1, -1);
}

static void stopTrack(Music* music)
{
    tic_api_music(music->tic, -1, -1, -1, music->loop, music->sustain, -1, -1);
}

static void toggleFollowMode(Music* music)
{
    music->follow = !music->follow;
}

static void toggleSustainMode(Music* music)
{
    music->tic->ram->music_state.flag.music_sustain = !music->sustain;
    music->sustain = !music->sustain;
}

static void toggleLoopMode(Music* music)
{
    music->loop = !music->loop;
}

static void resetSelection(Music* music)
{
    music->tracker.select.start = (tic_point){-1, -1};
    music->tracker.select.rect = (tic_rect){0, 0, 0, 0};
}

static void deleteSelection(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern)
    {
        tic_rect rect = music->tracker.select.rect;

        if(rect.h <= 0)
        {
            rect.y = music->tracker.edit.y;
            rect.h = 1;
        }

        memset(&pattern->rows[rect.y], 0, sizeof(tic_track_row) * rect.h);
    }
}

typedef struct
{
    u8 size;
} ClipboardHeader;

static void copyToClipboard(Music* music, bool cut)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern)
    {
        tic_rect rect = music->tracker.select.rect;

        if(rect.h <= 0)
        {
            rect.y = music->tracker.edit.y;
            rect.h = 1;
        }

        ClipboardHeader header = {rect.h};

        enum{RowSize = sizeof(tic_track_pattern) / MUSIC_PATTERN_ROWS, HeaderSize = sizeof(ClipboardHeader)};

        s32 size = rect.h * RowSize + HeaderSize;
        u8* data = malloc(size);

        if(data)
        {
            memcpy(data, &header, HeaderSize);
            memcpy(data + HeaderSize, &pattern->rows[rect.y], RowSize * rect.h);

            toClipboard(data, size, true);

            free(data);

            if(cut)
            {
                deleteSelection(music);
                history_add(music->history);
            }

            resetSelection(music);
        }       
    }
}

static void copyFromClipboard(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern && tic_sys_clipboard_has())
    {
        char* clipboard = tic_sys_clipboard_get();

        if(clipboard)
        {
            s32 size = (s32)strlen(clipboard)/2;

            enum{RowSize = sizeof(tic_track_pattern) / MUSIC_PATTERN_ROWS, HeaderSize = sizeof(ClipboardHeader)};

            if(size > HeaderSize)
            {
                u8* data = malloc(size);

                tic_tool_str2buf(clipboard, (s32)strlen(clipboard), data, true);

                ClipboardHeader header = {0};

                memcpy(&header, data, HeaderSize);

                if(header.size * RowSize == size - HeaderSize)
                {
                    if(header.size + music->tracker.edit.y > MUSIC_PATTERN_ROWS)
                        header.size = MUSIC_PATTERN_ROWS - music->tracker.edit.y;

                    memcpy(&pattern->rows[music->tracker.edit.y], data + HeaderSize, header.size * RowSize);
                    history_add(music->history);
                }

                free(data);
            }

            tic_sys_clipboard_free(clipboard);
        }
    }
}

static void setChannelPatternValue(Music* music, s32 pattern, s32 frame, s32 channel)
{
    if(pattern < 0) pattern = MUSIC_PATTERNS;
    if(pattern > MUSIC_PATTERNS) pattern = 0;

    tic_tool_set_pattern_id(getTrack(music), frame, channel, pattern);
    history_add(music->history);
}

static void prevPattern(Music* music)
{
    s32 channel = music->tracker.edit.x / CHANNEL_COLS;

    if (channel > 0)
    {
        music->tracker.edit.x = (channel-1) * CHANNEL_COLS;
        music->tracker.col = 1;
    }
}

static void nextPattern(Music* music)
{
    s32 channel = music->tracker.edit.x / CHANNEL_COLS;

    if (channel < TIC_SOUND_CHANNELS-1)
    {
        music->tracker.edit.x = (channel+1) * CHANNEL_COLS;
        music->tracker.col = 0;
    }
}

static void colLeft(Music* music)
{
    if(music->tracker.col > 0)
        music->tracker.col--;
    else prevPattern(music);
}

static void colRight(Music* music)
{
    if(music->tracker.col < 1)
        music->tracker.col++;
    else nextPattern(music);
}

static void startSelection(Music* music)
{
    if(music->tracker.select.start.x < 0 || music->tracker.select.start.y < 0)
    {
        music->tracker.select.start.x = music->tracker.edit.x;
        music->tracker.select.start.y = music->tracker.edit.y;
    }
}

static void updateSelection(Music* music)
{
    s32 rl = MIN(music->tracker.edit.x, music->tracker.select.start.x);
    s32 rt = MIN(music->tracker.edit.y, music->tracker.select.start.y);
    s32 rr = MAX(music->tracker.edit.x, music->tracker.select.start.x);
    s32 rb = MAX(music->tracker.edit.y, music->tracker.select.start.y);

    tic_rect* rect = &music->tracker.select.rect;
    *rect = (tic_rect){rl, rt, rr - rl + 1, rb - rt + 1};

    if(rect->x % CHANNEL_COLS + rect->w > CHANNEL_COLS)
        resetSelection(music);
}

static s32 getDigit(s32 pos, s32 val)
{
    enum {Base = 10};

    s32 div = 1;
    while(pos--) div *= Base;

    return val / div % Base;
}

static s32 setDigit(s32 pos, s32 val, s32 digit)
{
    enum {Base = 10};

    s32 div = 1;
    while(pos--) div *= Base;

    return val - (val / div % Base - digit) * div;
}

static s32 sym2dec(char sym)
{
    s32 val = -1;
    if (sym >= '0' && sym <= '9') val = sym - '0';
    return val;
}

static s32 sym2hex(char sym)
{
    s32 val = sym2dec(sym);
    if (sym >= 'a' && sym <= 'f') val = sym - 'a' + 10;

    return val;
}

static void delete(Music* music)
{
    deleteSelection(music);

    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern)
    {
        history_add(music->history);

        if(music->tracker.select.rect.h <= 0)
            downRow(music);        
    }
}

static void backspace(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern)
    {
        tic_track_row* rows = pattern->rows;
        const tic_rect* rect = &music->tracker.select.rect;

        if(rect->h > 0)
        {
            memmove(&rows[rect->y], &rows[rect->y + rect->h], (MUSIC_PATTERN_ROWS - (rect->y + rect->h)) * sizeof(tic_track_row));
            memset(&rows[MUSIC_PATTERN_ROWS - rect->h], 0, rect->h * sizeof(tic_track_row));
            music->tracker.edit.y = rect->y;
        }
        else
        {
            s32 y = music->tracker.edit.y;

            if(y >= 1)
            {
                memmove(&rows[y - 1], &rows[y], (MUSIC_PATTERN_ROWS - y) * sizeof(tic_track_row));
                memset(&rows[MUSIC_PATTERN_ROWS - 1], 0, sizeof(tic_track_row));
                upRow(music);
            }
        }

        history_add(music->history);
    }
}

static void insert(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern)
    {
        s32 y = music->tracker.edit.y;

        enum{Max = MUSIC_PATTERN_ROWS - 1};

        if(y < Max)
        {
            tic_track_row* rows = pattern->rows;
            memmove(&rows[y + 1], &rows[y], (Max - y) * sizeof(tic_track_row));
            memset(&rows[y], 0, sizeof(tic_track_row));
            history_add(music->history);
        }
    }
}

static tic_track_row* startRow(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    const tic_rect* rect = &music->tracker.select.rect;
    return pattern ? pattern->rows + (rect->h > 0 ? rect->y : music->tracker.edit.y) : NULL;
}

static tic_track_row* endRow(Music* music)
{
    tic_track_pattern* pattern = getChannelPattern(music);
    const tic_rect* rect = &music->tracker.select.rect;
    return pattern ? startRow(music) + (rect->h > 0 ? rect->h : 1) : NULL;
}

static void incNote(Music* music, s32 note, s32 octave)
{
    for(tic_track_row* row = startRow(music), *end = endRow(music); row < end; row++)
    {
        if(row && row->note >= NoteStart)
        {
            s32 index = (row->note + note - NoteStart) + (row->octave + octave) * NOTES;

            if(index >= 0)
            {
                row->note = (index % NOTES) + NoteStart;
                row->octave = index / NOTES;                    
            }
        }
    }

    history_add(music->history);
}

static void decSemitone(Music* music)   { incNote(music, -1, 0); }
static void incSemitone(Music* music)   { incNote(music, +1, 0); }
static void decOctave(Music* music)     { incNote(music, 0, -1); }
static void incOctave(Music* music)     { incNote(music, 0, +1); }

static void incSfx(Music* music, s32 inc)
{
    for(tic_track_row* row = startRow(music), *end = endRow(music); row < end; row++)
        if(row && row->note >= NoteStart)
            tic_tool_set_track_row_sfx(row, tic_tool_get_track_row_sfx(row) + inc);

    history_add(music->history);
}

static void upSfx(Music* music)     { incSfx(music, +1); }
static void downSfx(Music* music)   { incSfx(music, -1); }

static void setChannelPattern(Music* music, s32 delta, s32 channel)
{
    s32 pattern = tic_tool_get_pattern_id(getTrack(music), music->frame, channel);
    setChannelPatternValue(music, pattern + delta, music->frame, channel);
}

static void processTrackerKeyboard(Music* music)
{
    tic_mem* tic = music->tic;

    bool shift = tic_api_key(tic, tic_key_shift);
    bool ctrl = tic_api_key(tic, tic_key_ctrl);

    static const struct Handler{u8 key; void(*handler)(Music* music); bool select; bool ctrl;} Handlers[] = 
    {
        {tic_key_up,        upRow,          true},
        {tic_key_down,      downRow,        true},
        {tic_key_up,        upSfx,          false, true},
        {tic_key_down,      downSfx,        false, true},
        {tic_key_left,      leftCol,        true},
        {tic_key_right,     rightCol,       true},
        {tic_key_left,      upFrame,        false, true},
        {tic_key_right,     downFrame,      false, true},
        {tic_key_home,      goHome,         true},
        {tic_key_end,       goEnd,          true},
        {tic_key_pageup,    pageUp,         true},
        {tic_key_pagedown,  pageDown,       true},
        {tic_key_tab,       doTab},
        {tic_key_delete,    delete},
        {tic_key_backspace, backspace},
        {tic_key_insert,    insert},
        {tic_key_f1,        decSemitone,    false, true},
        {tic_key_f2,        incSemitone,    false, true},
        {tic_key_f3,        decOctave,      false, true},
        {tic_key_f4,        incOctave,      false, true},
    };

    for(const struct Handler *ptr = Handlers, *end = ptr + COUNT_OF(Handlers); ptr < end; ptr++)
        if(keyWasPressed(music->studio, ptr->key))
        {
            if(shift && ptr->select)
                startSelection(music);

            if(ptr->ctrl == ctrl)
                ptr->handler(music);

            if(shift)
            {
                if(ptr->select)
                    updateSelection(music);
            }
            else if(!ctrl)
                resetSelection(music);
        }

    static const u8 Piano[] =
    {
        tic_key_z,
        tic_key_s,
        tic_key_x,
        tic_key_d,
        tic_key_c,
        tic_key_v,
        tic_key_g,
        tic_key_b,
        tic_key_h,
        tic_key_n,
        tic_key_j,
        tic_key_m,

        // octave +1
        tic_key_q,
        tic_key_2,
        tic_key_w,
        tic_key_3,
        tic_key_e,
        tic_key_r,
        tic_key_5,
        tic_key_t,
        tic_key_6,
        tic_key_y,
        tic_key_7,
        tic_key_u,
        
        // extra keys
        tic_key_i,
        tic_key_9,
        tic_key_o,
        tic_key_0,
        tic_key_p,
    };

    if (getChannelPattern(music) && !ctrl)
    {
        s32 col = music->tracker.edit.x % CHANNEL_COLS;

        switch (col)
        {
        case ColumnNote:
        case ColumnSemitone:
            if (keyWasPressed(music->studio, tic_key_1) || keyWasPressed(music->studio, tic_key_a))
            {
                setStopNote(music);
                downRow(music);
            }
            else
            {
                tic_track_pattern* pattern = getChannelPattern(music);

                for (s32 i = 0; i < COUNT_OF(Piano); i++)
                {
                    if (keyWasPressed(music->studio, Piano[i]))
                    {
                        s32 note = i % NOTES;
                        s32 octave = i / NOTES + music->last.octave;
                        s32 sfx = music->last.sfx;
                        setNote(music, note, octave, sfx);

                        downRow(music);

                        break;
                    }               
                }
            }
            break;
        case ColumnOctave:
            if(getNote(music) >= 0)
            {
                s32 octave = -1;

                char sym = getKeyboardText(music->studio);

                if(sym >= '1' && sym <= '8') octave = sym - '1';

                if(octave >= 0)
                {
                    setOctave(music, octave);
                    downRow(music);
                }
            }
            break;
        case ColumnSfxHi:
        case ColumnSfxLow:
            if(getNote(music) >= 0)
            {
                s32 val = sym2dec(getKeyboardText(music->studio));
                            
                if(val >= 0)
                {
                    s32 sfx = setDigit(col == 3 ? 1 : 0, getSfx(music), val);

                    setSfx(music, sfx);

                    if(col == 3) rightCol(music);
                    else downRow(music), leftCol(music);
                }
            }
            break;
        case ColumnCommand:
            {
                char sym = getKeyboardText(music->studio);

                if(sym)
                {
                    const char* val = strchr(MusicCommands, toupper(sym));
                                
                    if(val)
                        setCommand(music, val - MusicCommands);
                }
            }
            break;
        case ColumnParameter1:
        case ColumnParameter2:
            {
                s32 val = sym2hex(getKeyboardText(music->studio));

                if(val >= 0)
                {
                    col == ColumnParameter1
                        ? setParam1(music, val)
                        : setParam2(music, val);
                }
            }
            break;          
        }

        history_add(music->history);
    }

    switch (getKeyboardText(music->studio))
    {
    case '+': setChannelPattern(music, +1, music->tracker.edit.x / CHANNEL_COLS); break;
    case '-': setChannelPattern(music, -1, music->tracker.edit.x / CHANNEL_COLS); break;
    }

}

static void processPatternKeyboard(Music* music)
{
    tic_mem* tic = music->tic;
    s32 channel = music->tracker.edit.x / CHANNEL_COLS;

    if(tic_api_key(tic, tic_key_ctrl) || tic_api_key(tic, tic_key_alt))
        return;

    if(keyWasPressed(music->studio, tic_key_delete))       setChannelPatternValue(music, 0, music->frame, channel);
    else if(keyWasPressed(music->studio, tic_key_tab))     nextPattern(music);
    else if(keyWasPressed(music->studio, tic_key_left))    colLeft(music);
    else if(keyWasPressed(music->studio, tic_key_right))   colRight(music);
    else if(keyWasPressed(music->studio, tic_key_down) 
        || keyWasPressed(music->studio, tic_key_return)) 
        music->tracker.edit.y = music->scroll.pos;
    else
    {
        s32 val = sym2dec(getKeyboardText(music->studio));

        if(val >= 0)
        {
            s32 pattern = setDigit(1 - music->tracker.col & 1, tic_tool_get_pattern_id(getTrack(music), 
                music->frame, channel), val);

            if(pattern <= MUSIC_PATTERNS)
            {
                setChannelPatternValue(music, pattern, music->frame, channel);

                if(music->tracker.col == 0)
                    colRight(music);                     
            }
        }
    }

    switch (getKeyboardText(music->studio))
    {
    case '+': setChannelPattern(music, +1, music->tracker.edit.x / CHANNEL_COLS); break;
    case '-': setChannelPattern(music, -1, music->tracker.edit.x / CHANNEL_COLS); break;
    }
}

static inline s32 rowIndex(Music* music, s32 row)
{
    return row + music->scroll.pos;
}

static void selectAll(Music* music)
{
    resetSelection(music);

    s32 col = music->tracker.edit.x - music->tracker.edit.x % CHANNEL_COLS;

    music->tracker.select.start = (tic_point){col, 0};
    music->tracker.edit.x = col + CHANNEL_COLS-1;
    music->tracker.edit.y = MUSIC_PATTERN_ROWS-1;

    updateSelection(music);
}

static void processKeyboard(Music* music)
{
    tic_mem* tic = music->tic;

    switch(getClipboardEvent(music->studio))
    {
    case TIC_CLIPBOARD_CUT: copyToClipboard(music, true); break;
    case TIC_CLIPBOARD_COPY: copyToClipboard(music, false); break;
    case TIC_CLIPBOARD_PASTE: copyFromClipboard(music); break;
    default: break;
    }

    bool ctrl = tic_api_key(tic, tic_key_ctrl);
    bool shift = tic_api_key(tic, tic_key_shift);
    
    if(tic_api_key(tic, tic_key_alt))
        return;

    if (ctrl)
    {
        if(keyWasPressed(music->studio, tic_key_a))            selectAll(music);
        else if(keyWasPressed(music->studio, tic_key_z))       undo(music);
        else if(keyWasPressed(music->studio, tic_key_y))       redo(music);
        else if(keyWasPressed(music->studio, tic_key_f))       toggleFollowMode(music);
    }
    else
    {
        bool stopped = !checkPlaying(music);

        if(keyWasPressed(music->studio, tic_key_space))
        {
            stopped 
                ? playTrack(music)
                : stopTrack(music);
        }
        else if(keyWasPressed(music->studio, tic_key_return))
        {
            stopped
                ? (shift
                    ? playTrackFromNow(music) 
                    : playFrame(music))
                : stopTrack(music);
        }

        music->tracker.edit.y >= 0 
            ? processTrackerKeyboard(music)
            : processPatternKeyboard(music);
    }
}

static void setIndex(Music* music, s32 value)
{
    music->track = CLAMP(value, 0, MUSIC_TRACKS-1);
}

static void setTempo(Music* music, s32 value)
{
    enum
    {
        Min = 40-DEFAULT_TEMPO,
        Max = 250-DEFAULT_TEMPO,
    };

    value -= DEFAULT_TEMPO;
    tic_track* track = getTrack(music);
    track->tempo = CLAMP(value, Min, Max);

    history_add(music->history);
}

static void setSpeed(Music* music, s32 value)
{
    enum
    {
        Min = 1-DEFAULT_SPEED,
        Max = 31-DEFAULT_SPEED,
    };

    value -= DEFAULT_SPEED;
    tic_track* track = getTrack(music);
    track->speed = CLAMP(value, Min, Max);

    history_add(music->history);
}

static void setRows(Music* music, s32 value)
{
    enum
    {
        Min = 0,
        Max = MUSIC_PATTERN_ROWS - TRACKER_ROWS,
    };

    value = MUSIC_PATTERN_ROWS - value;
    tic_track* track = getTrack(music);
    track->rows = CLAMP(value, Min, Max);
    updateTracker(music);

    history_add(music->history);
}

static void drawTopPanel(Music* music, s32 x, s32 y)
{
    tic_track* track = getTrack(music);

    drawSwitch(music, x, y, "TRACK", music->track, setIndex);
    drawSwitch(music, x += TIC_FONT_WIDTH * 9, y, "TEMPO", track->tempo + DEFAULT_TEMPO, setTempo);
    drawSwitch(music, x += TIC_FONT_WIDTH * 10, y, "SPD", track->speed + DEFAULT_SPEED, setSpeed);
    drawSwitch(music, x += TIC_FONT_WIDTH * 7, y, "ROWS", MUSIC_PATTERN_ROWS - track->rows, setRows);
}

static void drawTrackerFrames(Music* music, s32 x, s32 y)
{
    tic_mem* tic = music->tic;
    enum
    {
        Border = 1,
        Width = TIC_FONT_WIDTH * 2 + Border,
    };

    {
        tic_rect rect = { x - Border, y - Border, Width, MUSIC_FRAMES * TIC_FONT_HEIGHT + Border };

        if (checkMousePos(music->studio, &rect))
        {
            setCursor(music->studio, tic_cursor_hand);

            showTooltip(music->studio, "select frame");

            if (checkMouseDown(music->studio, &rect, tic_mouse_left))
            {
                s32 my = tic_api_mouse(tic).y - rect.y - Border;
                music->frame = my / TIC_FONT_HEIGHT;
            }
        }

        drawEditPanel(music, rect.x, rect.y, rect.w, rect.h);
    }

    for (s32 i = 0; i < MUSIC_FRAMES; i++)
    {
        if (checkPlayFrame(music, i))
        {
            drawBitIcon(music->studio, tic_icon_right, x - TIC_FONT_WIDTH-2, y + i*TIC_FONT_HEIGHT, tic_color_black);
            drawBitIcon(music->studio, tic_icon_right, x - TIC_FONT_WIDTH-2, y - 1 + i*TIC_FONT_HEIGHT, tic_color_white);
        }

        char buf[sizeof "99"];
        sprintf(buf, "%02i", i);

        tic_api_print(music->tic, buf, x, y + i*TIC_FONT_HEIGHT, i == music->frame ? tic_color_white : tic_color_grey, true, 1, false);
    }

    if(music->tracker.edit.y >= 0)
    {
        char buf[sizeof "99"];
        sprintf(buf, "%02i", music->tracker.edit.y);
        tic_api_print(music->tic, buf, x, y - 11, tic_color_black, true, 1, false);
        tic_api_print(music->tic, buf, x, y - 12, tic_color_white, true, 1, false);
    }
}

static inline void drawChar(tic_mem* tic, char symbol, s32 x, s32 y, u8 color, bool alt)
{
    tic_api_print(tic, (char[]){symbol, '\0'}, x, y, color, true, 1, alt);
}

static inline bool noteBeat(Music* music, s32 row)
{
    return row % (music->beat34 ? 3 : 4) == 0;
}

static void drawTrackerChannel(Music* music, s32 x, s32 y, s32 channel)
{
    tic_mem* tic = music->tic;

    enum
    {
        Border = 1,
        Rows = TRACKER_ROWS,
        Width = TIC_FONT_WIDTH * 8 + Border,
    };

    tic_rect rect = {x - Border, y - Border, Width, Rows*TIC_FONT_HEIGHT + Border};

    if(checkMousePos(music->studio, &rect))
    {
        setCursor(music->studio, tic_cursor_hand);

        if(checkMouseDown(music->studio, &rect, tic_mouse_left))
        {
            s32 mx = tic_api_mouse(tic).x - rect.x - Border;
            s32 my = tic_api_mouse(tic).y - rect.y - Border;

            s32 col = music->tracker.edit.x = channel * CHANNEL_COLS + mx / TIC_FONT_WIDTH;
            s32 row = music->tracker.edit.y = my / TIC_FONT_HEIGHT + music->scroll.pos;

            if(music->tracker.select.drag)
            {
                updateSelection(music);
            }
            else
            {
                resetSelection(music);
                music->tracker.select.start = (tic_point){col, row};

                music->tracker.select.drag = true;
            }
        }
    }

    if(music->tracker.select.drag)
    {
        tic_rect rect = {0, 0, TIC80_WIDTH, TIC80_HEIGHT};
        if(!checkMouseDown(music->studio, &rect, tic_mouse_left))
        {
            music->tracker.select.drag = false;
        }
    }

    drawEditPanel(music, rect.x, rect.y, rect.w, rect.h);

    s32 start = music->scroll.pos;
    s32 end = start + Rows;
    bool selectedChannel = music->tracker.select.rect.x / CHANNEL_COLS == channel;

    tic_track_pattern* pattern = getPattern(music, channel);

    for (s32 i = start, pos = 0; i < end; i++, pos++)
    {
        s32 rowy = y + pos*TIC_FONT_HEIGHT;

        if (i == music->tracker.edit.y)
        {
            tic_api_rect(music->tic, x - 1, rowy - 1, Width, TIC_FONT_HEIGHT + 1, tic_color_dark_grey);
        }

        // draw selection
        if (selectedChannel)
        {
            tic_rect rect = music->tracker.select.rect;
            if (rect.h > 1 && i >= rect.y && i < rect.y + rect.h)
            {
                s32 sx = x - 1;
                tic_api_rect(tic, sx, rowy - 1, CHANNEL_COLS * TIC_FONT_WIDTH + 1, TIC_FONT_HEIGHT + 1, tic_color_grey);
            }
        }

        if (checkPlayRow(music, i))
        {
            tic_api_rect(music->tic, x - 1, rowy - 1, Width, TIC_FONT_HEIGHT + 1, tic_color_white);
        }

        char rowStr[] = "--------";

        if (pattern)
        {
            static const char* Notes[] = SFX_NOTES;

            const tic_track_row* row = &pattern->rows[i];
            s32 note = row->note;
            s32 octave = row->octave;
            s32 sfx = tic_tool_get_track_row_sfx(&pattern->rows[i]);

            if (note == NoteStop)
                strcpy(rowStr, "     ---");

            if (note >= NoteStart)
                sprintf(rowStr, "%s%i%02i---", Notes[note - NoteStart], octave + 1, sfx);

            if(row->command > tic_music_cmd_empty)
                sprintf(rowStr+5, "%c%01X%01X", MusicCommands[row->command], row->param1, row->param2);

            static const u8 Colors[] = { tic_color_light_green, tic_color_yellow, tic_color_light_blue };
            static const u8 DarkColors[] = { tic_color_green, tic_color_orange, tic_color_blue };
            static u8 ColorIndexes[] = { 0, 0, 0, 1, 1, 2, 2, 2 };

            bool beetRow = noteBeat(music, i);

            for (s32 c = 0, colx = x; c < sizeof rowStr - 1; c++, colx += TIC_FONT_WIDTH)
            {
                char sym = rowStr[c];
                const u8* colors = beetRow || sym != '-' ? Colors : DarkColors;

                drawChar(music->tic, sym, colx, rowy, colors[ColorIndexes[c]], false);
            }
        }
        else tic_api_print(music->tic, rowStr, x, rowy, i == music->tracker.edit.y ? tic_color_black : tic_color_dark_grey, true, 1, false);

        if (i == music->tracker.edit.y)
        {
            if (music->tracker.edit.x / CHANNEL_COLS == channel)
            {
                s32 col = music->tracker.edit.x % CHANNEL_COLS;
                s32 colx = x - 1 + col * TIC_FONT_WIDTH;
                tic_api_rect(music->tic, colx, rowy - 1, TIC_FONT_WIDTH + 1, TIC_FONT_HEIGHT + 1, tic_color_red);
                drawChar(music->tic, rowStr[col], colx + 1, rowy, tic_color_black, false);
            }
        }

        if (noteBeat(music, i))
            tic_api_pix(music->tic, x - 4, y + pos*TIC_FONT_HEIGHT + 2, tic_color_black, false);
    }
}

static void drawTumbler(Music* music, s32 x, s32 y, s32 index)
{
    tic_mem* tic = music->tic;

    enum{On=36, Off = 52, Width=7, Height=3};
    
    tic_rect rect = {x, y, Width, Height};

    if(checkMousePos(music->studio, &rect))
    {
        setCursor(music->studio, tic_cursor_hand);

        showTooltip(music->studio, "on/off channel");

        if(checkMouseClick(music->studio, &rect, tic_mouse_left))
        {
            if (tic_api_key(tic, tic_key_ctrl))
            {
                for (s32 i = 0; i < TIC_SOUND_CHANNELS; i++)
                    music->on[i] = i == index;
            }
            else music->on[index] = !music->on[index];
        }
    }

    drawEditPanel(music, x, y, Width, Height);

    u8 color = tic_color_black;
    tiles2ram(tic->ram, &getConfig(music->studio)->cart->bank0.tiles);
    tic_api_spr(tic, music->on[index] ? On : Off, x, y, 1, 1, &color, 1, 1, tic_no_flip, tic_no_rotate);
}

static void drawTrackerLayout(Music* music, s32 x, s32 y)
{
    drawTrackerFrames(music, x, y);

    x += TIC_FONT_WIDTH * 3;

    enum{ChannelWidth = TIC_FONT_WIDTH * 9};

    for (s32 i = 0; i < TIC_SOUND_CHANNELS; i++)
    {
        s32 patternId = tic_tool_get_pattern_id(getTrack(music), music->frame, i);
        drawEditbox(music, x + ChannelWidth * i + 3*TIC_FONT_WIDTH, y - 12, patternId, setChannelPattern, i);
        drawTumbler(music, x + ChannelWidth * i + 7*TIC_FONT_WIDTH-1, y - 11, i);
    }

    for (s32 i = 0; i < TIC_SOUND_CHANNELS; i++)
        drawTrackerChannel(music, x + ChannelWidth * i, y, i);
}

static void drawPlayButtons(Music* music)
{
    typedef struct
    {
        u8 icon; 
        const char* tip; 
        const char* alt; 
        void(*handler)(Music*);
    } Button;

    static const Button FollowButton = 
    {
        tic_icon_follow,
        "FOLLOW [ctrl+f]",
        NULL,
        toggleFollowMode,
    };

    static const Button LoopButton = 
    {
        tic_icon_loop,
        "LOOP",
        NULL,
        toggleLoopMode,
    };

    static const Button SustainButton = 
    {
        tic_icon_sustain,
        "SUSTAIN NOTES ...",
        "BETWEEN FRAMES",
        toggleSustainMode,
    };

    static const Button PlayFromNowButton = 
    {
        tic_icon_playnow,
        "PLAY FROM NOW ...",
        "... [shift+enter]",
        playTrackFromNow,
    };

    static const Button PlayFrameButton = 
    {
        tic_icon_playframe,
        "PLAY FRAME ...",
        "... [enter]",
        playFrame,
    };

    static const Button PlayTrackButton = 
    {
        tic_icon_right,
        "PLAY TRACK ...",
        "... [space]",
        playTrack,
    };

    static const Button StopButton = 
    {
        tic_icon_stop,
        "STOP [enter]",
        NULL,
        stopTrack,
    };

    const Button **start, **end;

    if(checkPlaying(music))
    {
        static const Button* Buttons[] = 
        {
            &LoopButton, 
            &FollowButton, 
            &SustainButton,
            &StopButton,
        };

        start = Buttons;
        end = start + COUNT_OF(Buttons);
    }
    else
    {
        static const Button* Buttons[] = 
        {
            &LoopButton, 
            &FollowButton, 
            &SustainButton,
            &PlayFromNowButton,
            &PlayFrameButton,
            &PlayTrackButton,
        };

        start = Buttons;
        end = start + COUNT_OF(Buttons);
    }

    tic_rect rect = { TIC80_WIDTH - 54, 0, TIC_FONT_WIDTH, TOOLBAR_SIZE };

    for(const Button** btn = start; btn < end; btn++, rect.x += TIC_FONT_WIDTH)
    {
        bool over = false;

        if (checkMousePos(music->studio, &rect))
        {
            setCursor(music->studio, tic_cursor_hand);
            over = true;

            showTooltip(music->studio, (*btn)->alt && music->tickCounter % (TIC80_FRAMERATE * 2) < TIC80_FRAMERATE ? (*btn)->alt : (*btn)->tip);

            if (checkMouseClick(music->studio, &rect, tic_mouse_left))
                (*btn)->handler(music);
        }

        tic_color color = *btn == &FollowButton && music->follow
            || *btn == &SustainButton && music->sustain
            || *btn == &LoopButton && music->loop
                ? tic_color_green
                : over ? tic_color_grey : tic_color_light_grey;

        drawBitIcon(music->studio, (*btn)->icon, rect.x, rect.y, color);
    }
}

static void drawMusicToolbar(Music* music)
{
    tic_api_rect(music->tic, 0, 0, TIC80_WIDTH, TOOLBAR_SIZE, tic_color_white);

    drawPlayButtons(music);
    //drawModeTabs(music);
}

static const char* getPatternLabel(Music* music, s32 frame, s32 channel)
{
    static char index[sizeof "--"];

    strcpy(index, "--");

    s32 pattern = tic_tool_get_pattern_id(getTrack(music), frame, channel);

    if(pattern)
        sprintf(index, "%02i", pattern);

    return index;
}

static void drawStereoSeparator(Music* music, s32 x, s32 y)
{
    tic_mem* tic = music->tic;
    static u8 Colors[] = 
    {
        tic_color_dark_green, tic_color_green, tic_color_light_green, tic_color_light_green
    };

    for(s32 i = 0; i < COUNT_OF(Colors); i++)
        tic_api_rect(tic, x + i * 4, y, 4, 1, Colors[i]);

    for(s32 i = COUNT_OF(Colors)-1; i >= 0; i--)
        tic_api_rect(tic, x + 27 - i * 4, y, 4, 1, Colors[i]);
}

static void drawBeatButton(Music* music, s32 x, s32 y)
{
    tic_mem* tic = music->tic;

    static const char Label44[] = "4/4";
    static const char Label34[] = "3/4";

    tic_rect rect = {x, y, (sizeof Label44 - 1) * TIC_ALTFONT_WIDTH - 1, TIC_FONT_HEIGHT - 1};

    bool down = false;
    if(checkMousePos(music->studio, &rect))
    {
        setCursor(music->studio, tic_cursor_hand);
        showTooltip(music->studio, music->beat34 ? "set 4 quarter note" : "set 3 quarter note");

        if(checkMouseDown(music->studio, &rect, tic_mouse_left))
            down = true;

        if(checkMouseClick(music->studio, &rect, tic_mouse_left))
            music->beat34 = !music->beat34;
    }

    tic_api_print(tic, music->beat34 ? Label34 : Label44, x, y + 1, tic_color_black, true, 1, true);
    tic_api_print(tic, music->beat34 ? Label34 : Label44, x, y + (down ? 1 : 0), tic_color_white, true, 1, true);
}

static void scrollNotes(Music* music, s32 delta)
{
    tic_track_pattern* pattern = getChannelPattern(music);

    if(pattern)
    {
        tic_rect rect = music->tracker.select.rect;

        if(rect.h <= 0)
        {
            rect.y = music->tracker.edit.y;
            rect.h = 1;
        }
        
        for(s32 i = rect.y; i < rect.y + rect.h; i++)
        {
            s32 note = pattern->rows[i].note + pattern->rows[i].octave * NOTES - NoteStart;

            note += delta;

            if(note >= 0 && note < NOTES*OCTAVES)
            {
                pattern->rows[i].note = note % NOTES + NoteStart;
                pattern->rows[i].octave = note / NOTES;
            }
        }

        history_add(music->history);
    }
}

static void drawWaveform(Music* music, s32 x, s32 y)
{
    tic_mem* tic = music->tic;

    enum{Width = 32, Height = 8, WaveRows = 1 << WAVE_VALUE_BITS};

    drawEditPanel(music, x, y, Width, Height);

    // detect playing channels
    s32 channels = 0;
    for(s32 c = 0; c < TIC_SOUND_CHANNELS; c++)
        if(music->on[c] && tic->ram->registers[c].volume)
            channels++;

    if(channels)
    {
        for(s32 i = 0; i < WAVE_VALUES; i++)
        {
            s32 lamp = 0, ramp = 0;

            for(s32 c = 0; c < TIC_SOUND_CHANNELS; c++)
            {
                s32 amp = calcWaveAnimation(tic, i + music->tickCounter, c) / channels;

                lamp += amp * tic_tool_peek4(&tic->ram->stereo.data, c*2);
                ramp += amp * tic_tool_peek4(&tic->ram->stereo.data, c*2 + 1);
            }

            lamp /= WAVE_MAX_VALUE * WAVE_MAX_VALUE;
            ramp /= WAVE_MAX_VALUE * WAVE_MAX_VALUE;

            tic_api_rect(tic, x + i, y + (Height-1) - ramp * Height / WaveRows, 1, 1, tic_color_yellow);
            tic_api_rect(tic, x + i, y + (Height-1) - lamp * Height / WaveRows, 1, 1, tic_color_light_green);
        }
    }
}

static void tick(Music* music)
{
    tic_mem* tic = music->tic;

    // process scroll
    {
        tic80_input* input = &tic->ram->input;

        if(input->mouse.scrolly)
        {
            if(tic_api_key(tic, tic_key_ctrl))
            {
                scrollNotes(music, input->mouse.scrolly > 0 ? 1 : -1);
            }
            else
            {       
                enum{Scroll = NOTES_PER_BEAT};
                s32 delta = input->mouse.scrolly > 0 ? -Scroll : Scroll;

                music->scroll.pos += delta;

                updateScroll(music);
            }
        }
    }

    processKeyboard(music);

    if(music->follow)
    {
        const tic_music_state* pos = getMusicPos(music);

        if(pos->music.track == music->track && 
            music->tracker.edit.y >= 0 &&
            pos->music.row >= 0)
        {
            music->frame = pos->music.frame;
            music->tracker.edit.y = pos->music.row;
            updateTracker(music);
        }
    }

    for (s32 i = 0; i < TIC_SOUND_CHANNELS; i++)
        if(!music->on[i])
            tic->ram->registers[i].volume = 0;

    tic_api_cls(music->tic, tic_color_grey);
    drawTopPanel(music, 2, TOOLBAR_SIZE + 3);
    drawWaveform(music, 205, 9);

    drawTrackerLayout(music, 7, 35);

    drawMusicToolbar(music);
    drawToolbar(music->studio, music->tic, false);

    music->tickCounter++;
}

static void onStudioEvent(Music* music, StudioEvent event)
{
    switch (event)
    {
    case TIC_TOOLBAR_CUT: copyToClipboard(music, true); break;
    case TIC_TOOLBAR_COPY: copyToClipboard(music, false); break;
    case TIC_TOOLBAR_PASTE: copyFromClipboard(music); break;
    case TIC_TOOLBAR_UNDO: undo(music); break;
    case TIC_TOOLBAR_REDO: redo(music); break;
    default: break;
    }
}

void initMusic(Music* music, Studio* studio, tic_music* src)
{
    if (music->history) history_delete(music->history);

    *music = (Music)
    {
        .studio = studio,
        .tic = getMemory(studio),
        .tick = tick,
        .src = src,
        .track = 0,
        .beat34 = false,
        .frame = 0,
        .follow = true,
        .sustain = false,
        .scroll = 
        {
            .pos = 0,
            .start = 0,
            .active = false,
        },
        .last =
        {
            .octave = 3,
            .sfx = 0,
        },
        .on = {true, true, true, true},
        .tracker =
        {
            .edit = {0, 0},

            .select = 
            {
                .start = {0, 0},
                .rect = {0, 0, 0, 0},
                .drag = false,
            },
        },

        .piano =
        {
            .col = 0,
            .edit = {0, 0},
            .note = {-1, -1, -1, -1},
        },

        .tickCounter = 0,
        //.tab = MUSIC_PIANO_TAB,
        .history = history_create(src, sizeof(tic_music)),
        .event = onStudioEvent,
    };

    resetSelection(music);
}

void freeMusic(Music* music)
{
    history_delete(music->history);
    free(music);
}
