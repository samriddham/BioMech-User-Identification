#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>       
#include <linux/fs.h>         
#include <linux/uaccess.h>    
#include <linux/sched.h>     
#include <linux/workqueue.h>  
#include <linux/input.h>   
#include <linux/input-event-codes.h> 
#include <linux/spinlock.h>   
#include <linux/file.h>     

#define LOG_FILE_PATH "/home/UTTU/NewProj/OSProj/Input_logger/OS_zip_file/output.csv"

#define MAX_BUFFER_SIZE 512

#define TMP_BUFF_SIZE 64

struct input_logger {
    struct file *log_file;
    struct input_handler input_handler;
    struct work_struct writer_task;
    char *current_buffer;
    char *write_buffer;
    size_t buffer_offset;
    size_t buffer_len;
    loff_t file_off;
    char side_buffer[MAX_BUFFER_SIZE];
    char back_buffer[MAX_BUFFER_SIZE];
    spinlock_t data_lock;
    int rel_x; int rel_y;
    int abs_x; int abs_y;
    int prev_abs_x; int prev_abs_y;
    bool shift_pressed; bool caps_lock_on;
};

static void input_event_handler(struct input_handle *handle, unsigned int type, unsigned int code, int value);
static int input_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id);
static void input_disconnect(struct input_handle *handle);
static void write_log_task(struct work_struct *work);
static size_t keycode_to_us_string(int keycode, bool shift, bool caps, char *buffer, size_t buff_size);
static void flush_buffer(struct input_logger *logger);

static const char *us_keymap[][2] = {
    {"\0", "\0"}, {"Escape", "Escape"}, {"1", "!"}, {"2", "@"}, 
    {"3", "#"}, {"4", "$"}, {"5", "%%"}, {"6", "^"}, 
    {"7", "&"}, {"8", "*"}, {"9", "("}, {"0", ")"}, 
    {"-", "_"}, {"=", "+"}, {"<backspace>", "<backspace>"}, 
    {"Tab", "Tab"}, {"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"}, 
    {"t", "T"}, {"y", "Y"}, {"u", "U"}, {"i", "I"}, 
    {"o", "O"}, {"p", "P"}, {"[", "{"}, {"]", "}"},
    {"", ""}, {"_LCTRL_", "_LCTRL_"}, {"a", "A"}, {"s", "S"},
    {"d", "D"}, {"f", "F"}, {"g", "G"}, {"h", "H"}, 
    {"j", "J"}, {"k", "K"}, {"l", "L"}, {";", ":"},
    {"'", "\""}, {"`", "~"}, {"shift", "shift"}, {"\\", "|"},
    {"z", "Z"}, {"x", "X"}, {"c", "C"}, {"v", "V"}, 
    {"b", "B"}, {"n", "N"}, {"m", "M"}, {",", "<"}, 
    {".", ">"}, {"/", "?"}, {"_RSHIFT_", "_RSHIFT_"}, {"_PRTSCR_", "_KPD*_"}, 
    {"_LALT_", "_LALT_"}, {" ", " "}, {"_CAPS_", "_CAPS_"}, {"F1", "F1"}, 
    {"F2", "F2"}, {"F3", "F3"}, {"F4", "F4"}, {"F5", "F5"},
    {"F6", "F6"}, {"F7", "F7"}, {"F8", "F8"}, {"F9", "F9"}, 
    {"F10", "F10"}, {"_NUM_", "_NUM_"}, {"_SCROLL_", "_SCROLL_"},
    {"_KPD7_", "_HOME_"}, {"_KPD8_", "_UP_"}, {"_KPD9_", "_PGUP_"}, 
    {"-", "-"}, {"_KPD4_", "_LEFT_"}, {"_KPD5_", "_KPD5_"},
    {"_KPD6_", "_RIGHT_"}, {"+", "+"}, {"_KPD1_", "_END_"}, 
    {"_KPD2_", "_DOWN_"}, {"_KPD3_", "_PGDN"}, {"_KPD0_", "_INS_"}, 
    {"_KPD._", "_DEL_"}, {"_SYSRQ_", "_SYSRQ_"}, {"\0", "\0"}, 
    {"\0", "\0"}, {"F11", "F11"}, {"F12", "F12"}, {"\0", "\0"},
    {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},
    {"\0", "\0"}, {"<enter>", "<enter>"}, {"_RCTRL_", "_RCTRL_"}, {"/", "/"},
    {"_PRTSCR_", "_PRTSCR_"}, {"_RALT_", "_RALT_"}, {"\0", "\0"},
    {"_HOME_", "_HOME_"}, {"<up>", "<up>"}, {"_PGUP_", "_PGUP_"},
    {"<left>", "<left>"}, {"<right>", "<right>"}, {"_END_", "_END_"},
    {"<down>", "<down>"}, {"_PGDN", "_PGDN"}, {"_INS_", "_INS_"},
    {"_DEL_", "_DEL_"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},
    {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},
    {"_PAUSE_", "_PAUSE_"},
};

static const struct input_device_id input_logger_ids[] = {
    { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT(EV_KEY) } },
    { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT(EV_REL) } },
    { .flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = { BIT(EV_ABS) } },
    { },
};
MODULE_DEVICE_TABLE(input, input_logger_ids);

static struct input_logger *logger_instance;

static void input_event_handler(struct input_handle *handle, unsigned int type, unsigned int code, int value) {
    struct input_logger *logger = logger_instance;
    char tmp_buff[TMP_BUFF_SIZE];
    size_t len = 0;
    bool needs_flush = false;

    if (!logger) return;

    spin_lock(&logger->data_lock);

    switch (type) {
        case EV_KEY: 
            if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) logger->shift_pressed = (value != 0);
            else if (code == KEY_CAPSLOCK && value == 1) logger->caps_lock_on = !logger->caps_lock_on;

            if (value == 1 || value == 2) {
                const char* event_str = NULL;
                switch (code) {
                    case BTN_LEFT:   event_str = NULL; break;
                    case BTN_RIGHT:  event_str = NULL; break;
                    case BTN_MIDDLE: event_str = NULL; break;
                    case BTN_SIDE:   event_str = "_BTN_SIDE_\n"; break;
                    case BTN_EXTRA:  event_str = "_BTN_EXTRA_\n"; break;
                }
                if (event_str) {
                    len = strlen(event_str);
                    if ((logger->buffer_offset + len) >= (MAX_BUFFER_SIZE - 1)) { needs_flush = true; }
                    else { strncpy(logger->current_buffer + logger->buffer_offset, event_str, len); logger->buffer_offset += len; }
                } else {
                    len = keycode_to_us_string(code, logger->shift_pressed, logger->caps_lock_on, tmp_buff, TMP_BUFF_SIZE);
                    if (len > 0) {
                        if ((logger->buffer_offset + len) >= (MAX_BUFFER_SIZE - 1)) { needs_flush = true; }
                        else { strncpy(logger->current_buffer + logger->buffer_offset, tmp_buff, len); logger->buffer_offset += len; if (tmp_buff[0] == '\n') needs_flush = true; }
                    }
                }
            }
            break; 

        case EV_REL:
            if (code == REL_WHEEL) {
                const char* scroll_str = (value > 0) ? "xdotool click 5\n" : "xdotool click 4\n";
                len = strlen(scroll_str);
                if ((logger->buffer_offset + len) >= (MAX_BUFFER_SIZE - 1)) {
                    needs_flush = true;
                } else {
                    strncpy(logger->current_buffer + logger->buffer_offset, scroll_str, len);
                    logger->buffer_offset += len;
                }
            } else if (code == REL_HWHEEL) {
                const char* hscroll_str = (value > 0) ? "_SCROLL_RIGHT_\n" : "_SCROLL_LEFT_\n";
                len = strlen(hscroll_str);
                if ((logger->buffer_offset + len) >= (MAX_BUFFER_SIZE - 1)) {
                    needs_flush = true;
                } else {
                    strncpy(logger->current_buffer + logger->buffer_offset, hscroll_str, len);
                    logger->buffer_offset += len;
                }
            } else if (code == REL_X) {
                logger->rel_x += value;
            } else if (code == REL_Y) {
                logger->rel_y += value;
            }
            break;

        case EV_ABS:
            if (code == ABS_X) logger->abs_x = value;
            else if (code == ABS_Y) logger->abs_y = value;
            break;

        case EV_SYN:
            if (code == SYN_REPORT) {
                if (logger->rel_x != 0 || logger->rel_y != 0) {
                    len = snprintf(tmp_buff, TMP_BUFF_SIZE,"mouse.move_relative_self(%d,%d)\n", logger->rel_x, logger->rel_y);
                    if (len > 0) { if (!needs_flush && (logger->buffer_offset + len) >= (MAX_BUFFER_SIZE - 1)) { needs_flush = true; } if (!needs_flush) { strncpy(logger->current_buffer + logger->buffer_offset, tmp_buff, len); logger->buffer_offset += len; } }
                    logger->rel_x = 0; logger->rel_y = 0;
                }
                if (logger->abs_x != logger->prev_abs_x || logger->abs_y != logger->prev_abs_y) {
                    len = snprintf(tmp_buff, TMP_BUFF_SIZE,"%d,%d\n", logger->abs_x, logger->abs_y);
                     if (len > 0) { if (!needs_flush && (logger->buffer_offset + len) >= (MAX_BUFFER_SIZE - 1)) { needs_flush = true; } if (!needs_flush) { strncpy(logger->current_buffer + logger->buffer_offset, tmp_buff, len); logger->buffer_offset += len; } }
                    logger->prev_abs_x = logger->abs_x; logger->prev_abs_y = logger->abs_y;
                }
                if (!needs_flush && logger->buffer_offset > 0 && logger->current_buffer[logger->buffer_offset - 1] == '\n') {
                    needs_flush = true;
                }
            }
            break;
    } 

    if (needs_flush) {
        flush_buffer(logger);
    }

    spin_unlock(&logger->data_lock);
}

static int input_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id) {
    struct input_handle *handle;
    int error;
    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle) { pr_err("input_logger: Failed handle alloc\n"); return -ENOMEM; }
    handle->dev = dev;
    handle->handler = handler;
    handle->name = "input_log_hdl";
    error = input_register_handle(handle);
    if (error) { pr_err("input_logger: Failed register handle for %s [%d]\n", dev->name, error); kfree(handle); return error; }
    error = input_open_device(handle);
    if (error) { pr_err("input_logger: Failed open device %s [%d]\n", dev->name, error); input_unregister_handle(handle); kfree(handle); return error; }
    pr_info("input_logger: Connected device: %s (Phys: %s)\n", dev->name, dev->phys);
    return 0;
}

static void input_disconnect(struct input_handle *handle) {
    pr_info("input_logger: Disconnected device: %s\n", handle->dev->name);
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static size_t keycode_to_us_string(int keycode, bool shift, bool caps, char *buffer, size_t buff_size) {
    const char *us_key = NULL;
    memset(buffer, 0x0, buff_size);
    if (keycode < 0 || keycode >= ARRAY_SIZE(us_keymap)) return 0;
    if (!us_keymap[keycode][0] && !us_keymap[keycode][1]) return 0;
    us_key = shift ? us_keymap[keycode][1] : us_keymap[keycode][0];
    if (shift && !us_key) us_key = us_keymap[keycode][0]; 
    if (!us_key) us_key = "_??_"; 
    if (caps && us_key[0] >= 'a' && us_key[0] <= 'z' && us_key[1] == '\0') {
        if (us_keymap[keycode][1] && us_keymap[keycode][1][0] >= 'A' && us_keymap[keycode][1][0] <= 'Z') {
            us_key = us_keymap[keycode][1];
        }
    } else if (caps && shift && us_key[0] >= 'A' && us_key[0] <= 'Z' && us_key[1] == '\0') {
         if (us_keymap[keycode][0] && us_keymap[keycode][0][0] >= 'a' && us_keymap[keycode][0][0] <= 'z') {
            us_key = us_keymap[keycode][0];
        }
    }
    snprintf(buffer, buff_size, "\0");
    return strnlen(buffer, buff_size - 1); 
}

void flush_buffer(struct input_logger *logger) {
    char *tmp;
    if (logger->buffer_offset > 0) {
        tmp = logger->current_buffer;
        logger->current_buffer = logger->write_buffer;
        logger->write_buffer = tmp;
        logger->buffer_len = logger->buffer_offset;
        schedule_work(&logger->writer_task);
    }
    logger->buffer_offset = 0; 
}

void write_log_task(struct work_struct *work) {
    struct input_logger *logger = container_of(work, struct input_logger, writer_task);
    if (!logger || !logger->log_file) { pr_err("input_logger: write_log_task invalid state\n"); return; }
    if (logger->buffer_len > 0) {
        kernel_write(logger->log_file, logger->write_buffer, logger->buffer_len, &logger->file_off);
        logger->buffer_len = 0;
    }
}

static int __init input_logger_init(void) {
    int error;
    pr_info("Loading Input Logger module (with scroll/clicks/Abs support)...\n"); 
    logger_instance = kzalloc(sizeof(struct input_logger), GFP_KERNEL);
    if (!logger_instance) { pr_err("input_logger: Failed memory alloc\n"); return -ENOMEM; }
    spin_lock_init(&logger_instance->data_lock);
    logger_instance->rel_x = 0; logger_instance->rel_y = 0;
    logger_instance->abs_x = 0; logger_instance->abs_y = 0;
    logger_instance->prev_abs_x = -1; logger_instance->prev_abs_y = -1;
    logger_instance->shift_pressed = false; logger_instance->caps_lock_on = false;
    INIT_WORK(&logger_instance->writer_task, write_log_task);
    logger_instance->current_buffer = logger_instance->side_buffer;
    logger_instance->write_buffer = logger_instance->back_buffer;
    logger_instance->buffer_offset = 0; logger_instance->buffer_len = 0;
    logger_instance->log_file = filp_open(LOG_FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (IS_ERR(logger_instance->log_file)) {
        pr_err("input_logger: Failed open log file %s [%ld]\n", LOG_FILE_PATH, PTR_ERR(logger_instance->log_file));
        error = PTR_ERR(logger_instance->log_file); kfree(logger_instance); logger_instance = NULL; return error;
    }
    logger_instance->file_off = logger_instance->log_file->f_pos;
    logger_instance->input_handler.event = input_event_handler;
    logger_instance->input_handler.connect = input_connect;
    logger_instance->input_handler.disconnect = input_disconnect;
    logger_instance->input_handler.name = "input_logger_hdlr";
    logger_instance->input_handler.id_table = input_logger_ids;
    error = input_register_handler(&logger_instance->input_handler);
    if (error) {
        pr_err("input_logger: Failed register handler [%d]\n", error);
        fput(logger_instance->log_file); kfree(logger_instance); logger_instance = NULL; return error;
    }
    pr_info("Input Logger module loaded successfully. Logging to %s\n", LOG_FILE_PATH);
    return 0;
}

static void __exit input_logger_exit(void) {
    pr_info("Unloading Input Logger module...\n");
    if (!logger_instance) return;
    input_unregister_handler(&logger_instance->input_handler);
    cancel_work_sync(&logger_instance->writer_task);
    spin_lock(&logger_instance->data_lock);
    flush_buffer(logger_instance);
    spin_unlock(&logger_instance->data_lock);
    cancel_work_sync(&logger_instance->writer_task);
    if (logger_instance->log_file) fput(logger_instance->log_file);
    kfree(logger_instance);
    logger_instance = NULL;
    pr_info("Input Logger module unloaded.\n");
}

module_init(input_logger_init);
module_exit(input_logger_exit);

