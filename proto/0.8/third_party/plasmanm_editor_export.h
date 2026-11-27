/* Our replacement for the export header that plasma-nm generates for libplasmanm_editor. */
#ifndef PLASMANM_EDITOR_EXPORT_H
#define PLASMANM_EDITOR_EXPORT_H
#define PLASMANM_EDITOR_EXPORT __attribute__((visibility("default")))
#endif
