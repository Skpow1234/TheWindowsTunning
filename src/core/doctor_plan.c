#include "core/doctor_plan.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <strsafe.h>

void wt_doctor_plan_init(WT_DoctorPlan *plan)
{
    if (plan == NULL) {
        return;
    }
    ZeroMemory(plan, sizeof(*plan));
}

static const char *wt_doctor_severity_name(WT_Severity severity)
{
    switch (severity) {
    case WT_SEVERITY_INFO:     return "info";
    case WT_SEVERITY_LOW:      return "low";
    case WT_SEVERITY_MEDIUM:   return "medium";
    case WT_SEVERITY_HIGH:     return "high";
    case WT_SEVERITY_CRITICAL: return "critical";
    default:                   return "info";
    }
}

static const char *wt_doctor_risk_name(WT_Risk risk)
{
    switch (risk) {
    case WT_RISK_NONE:   return "none";
    case WT_RISK_LOW:    return "low";
    case WT_RISK_MEDIUM: return "medium";
    case WT_RISK_HIGH:   return "high";
    default:             return "none";
    }
}

int wt_doctor_step_is_applyable(const WT_Recommendation *rec)
{
    if (rec == NULL || rec->action[0] == '\0') {
        return 0;
    }
    if (strcmp(rec->id, "WT-POWER-001") == 0 ||
        strcmp(rec->id, "WT-POWER-002") == 0) {
        return 1;
    }
    if (strncmp(rec->action, "wintune apply WT-STARTUP-DISABLE", 31) == 0 ||
        strncmp(rec->action, "wintune apply WT-STARTUP-DELAY", 29) == 0 ||
        strncmp(rec->action, "wintune apply WT-TASK-DISABLE", 28) == 0 ||
        strncmp(rec->action, "wintune apply WT-TASK-DELAY", 26) == 0) {
        return 1;
    }
    return 0;
}

/* Lower = earlier in the guided sequence. */
static int wt_doctor_category_rank(const char *id)
{
    if (id == NULL) {
        return 90;
    }
    if (strncmp(id, "WT-BLOCKER-", 11) == 0) {
        return 10;
    }
    if (strncmp(id, "WT-UPDATE-", 10) == 0) {
        return 15;
    }
    if (strncmp(id, "WT-POWER-", 9) == 0) {
        return 20;
    }
    if (strncmp(id, "WT-MEMORY-", 10) == 0 || strncmp(id, "WT-DISK-", 8) == 0 ||
        strncmp(id, "WT-CPU-", 7) == 0 || strncmp(id, "WT-GPU-", 7) == 0) {
        return 30;
    }
    if (strncmp(id, "WT-BOOT-", 8) == 0) {
        return 40;
    }
    if (strncmp(id, "WT-STARTUP-", 11) == 0) {
        return 50;
    }
    if (strncmp(id, "WT-TASK-", 8) == 0) {
        return 60;
    }
    return 80;
}

static int wt_doctor_step_less(const WT_DoctorPlanStep *a,
                               const WT_DoctorPlanStep *b)
{
    int ra = wt_doctor_category_rank(a->rec_id);
    int rb = wt_doctor_category_rank(b->rec_id);
    if (ra != rb) {
        return ra < rb;
    }
    if (a->severity != b->severity) {
        return a->severity > b->severity;
    }
    if (a->applyable != b->applyable) {
        return a->applyable > b->applyable;
    }
    return strcmp(a->rec_id, b->rec_id) < 0;
}

static void wt_doctor_fill_note(WT_DoctorPlanStep *step)
{
    if (step->applyable) {
        StringCchPrintfA(step->note, sizeof(step->note),
                         "Confirm this apply separately"
                         "%s. Preview with --dry-run first if unsure.",
                         step->requires_admin ? " (admin required)" : "");
    } else {
        StringCchCopyA(step->note, sizeof(step->note),
                       "Review only - no automatic apply. Follow the command.");
    }
}

static void wt_doctor_assign_depends(WT_DoctorPlan *plan)
{
    const char *last_blocker = NULL;
    const char *last_power = NULL;
    const char *last_startup = NULL;

    for (size_t i = 0; i < plan->count; ++i) {
        WT_DoctorPlanStep *s = &plan->steps[i];
        s->depends_on[0] = '\0';

        if (strncmp(s->rec_id, "WT-UPDATE-", 10) == 0 && last_blocker != NULL) {
            StringCchCopyA(s->depends_on, sizeof(s->depends_on), last_blocker);
        } else if (strncmp(s->rec_id, "WT-POWER-", 9) == 0 &&
                   last_blocker != NULL) {
            StringCchCopyA(s->depends_on, sizeof(s->depends_on), last_blocker);
        } else if (strncmp(s->rec_id, "WT-STARTUP-", 11) == 0) {
            if (last_power != NULL) {
                StringCchCopyA(s->depends_on, sizeof(s->depends_on), last_power);
            } else if (last_blocker != NULL) {
                StringCchCopyA(s->depends_on, sizeof(s->depends_on),
                               last_blocker);
            }
        } else if (strncmp(s->rec_id, "WT-TASK-", 8) == 0) {
            if (last_startup != NULL) {
                StringCchCopyA(s->depends_on, sizeof(s->depends_on),
                               last_startup);
            } else if (last_power != NULL) {
                StringCchCopyA(s->depends_on, sizeof(s->depends_on), last_power);
            }
        } else if (strncmp(s->rec_id, "WT-BOOT-", 8) == 0 &&
                   last_startup != NULL) {
            StringCchCopyA(s->depends_on, sizeof(s->depends_on), last_startup);
        }

        if (strncmp(s->rec_id, "WT-BLOCKER-", 11) == 0) {
            last_blocker = s->rec_id;
        } else if (strncmp(s->rec_id, "WT-POWER-", 9) == 0 && s->applyable) {
            last_power = s->rec_id;
        } else if (strncmp(s->rec_id, "WT-STARTUP-", 11) == 0) {
            last_startup = s->rec_id;
        }
    }
}

WT_Result wt_doctor_plan_build(const WT_RecommendationList *recs,
                               WT_DoctorPlan *out)
{
    if (recs == NULL || out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_doctor_plan_init(out);

    for (size_t i = 0; i < recs->count && out->count < WT_MAX_DOCTOR_STEPS; ++i) {
        const WT_Recommendation *r = &recs->items[i];
        WT_DoctorPlanStep *s = &out->steps[out->count];
        ZeroMemory(s, sizeof(*s));
        StringCchCopyA(s->rec_id, sizeof(s->rec_id), r->id);
        StringCchCopyA(s->title, sizeof(s->title), r->title);
        StringCchCopyA(s->command, sizeof(s->command), r->action);
        s->severity = r->severity;
        s->risk = r->risk;
        s->requires_admin = r->requires_admin;
        s->rollback_available = r->rollback_available;
        s->confidence_percent = r->confidence_percent;
        s->applyable = wt_doctor_step_is_applyable(r);
        wt_doctor_fill_note(s);
        if (s->applyable) {
            out->applyable_count++;
        } else {
            out->review_count++;
        }
        out->count++;
    }

    for (size_t i = 1; i < out->count; ++i) {
        WT_DoctorPlanStep key = out->steps[i];
        size_t j = i;
        while (j > 0 && wt_doctor_step_less(&key, &out->steps[j - 1])) {
            out->steps[j] = out->steps[j - 1];
            --j;
        }
        out->steps[j] = key;
    }

    for (size_t i = 0; i < out->count; ++i) {
        out->steps[i].order = (int)(i + 1);
    }
    wt_doctor_assign_depends(out);
    return WT_OK;
}

void wt_doctor_plan_print_text(FILE *out, const WT_DoctorPlan *plan)
{
    if (out == NULL || plan == NULL) {
        return;
    }

    fputs("\nGuided Doctor Plan\n", out);
    fputs("------------------\n", out);
    fprintf(out,
            "%zu step(s): %zu can be applied with confirmation, %zu review-only.\n"
            "WinTune never applies the whole plan in one shot - confirm each "
            "mutating step (or use --dry-run to preview).\n\n",
            plan->count, plan->applyable_count, plan->review_count);

    if (plan->count == 0) {
        fputs("No steps. Nothing needs attention based on the current samples.\n",
              out);
        return;
    }

    for (size_t i = 0; i < plan->count; ++i) {
        const WT_DoctorPlanStep *s = &plan->steps[i];
        fprintf(out, "%d. [%s] %s - %s\n", s->order, s->rec_id, s->title,
                s->applyable ? "APPLY" : "REVIEW");
        fprintf(out, "   Severity: %s  Risk: %s  Confidence: %d%%\n",
                wt_doctor_severity_name(s->severity),
                wt_doctor_risk_name(s->risk), s->confidence_percent);
        if (s->depends_on[0] != '\0') {
            fprintf(out, "   After: %s\n", s->depends_on);
        }
        fprintf(out, "   Command: %s\n", s->command);
        if (s->applyable) {
            fprintf(out, "   Preview: %s --dry-run\n", s->command);
        }
        fprintf(out, "   Note: %s\n\n", s->note);
    }
}

static void wt_json_esc(FILE *out, const char *s)
{
    if (s == NULL) {
        return;
    }
    for (const char *p = s; *p != '\0'; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            fputc('\\', out);
            fputc((char)c, out);
        } else if (c < 0x20) {
            fprintf(out, "\\u%04x", c);
        } else {
            fputc((char)c, out);
        }
    }
}

void wt_doctor_plan_print_json(FILE *out, const WT_DoctorPlan *plan)
{
    if (out == NULL || plan == NULL) {
        return;
    }
    fputs("{\n", out);
    fputs("  \"command\": \"doctor\",\n", out);
    fputs("  \"guided_plan\": {\n", out);
    fprintf(out, "    \"step_count\": %zu,\n", plan->count);
    fprintf(out, "    \"applyable_count\": %zu,\n", plan->applyable_count);
    fprintf(out, "    \"review_count\": %zu,\n", plan->review_count);
    fputs("    \"note\": \"Confirm each apply separately. Never fix-everything "
          "with a single --yes.\",\n",
          out);
    fputs("    \"steps\": [\n", out);
    for (size_t i = 0; i < plan->count; ++i) {
        const WT_DoctorPlanStep *s = &plan->steps[i];
        fputs("      {\n", out);
        fprintf(out, "        \"order\": %d,\n", s->order);
        fputs("        \"id\": \"", out);
        wt_json_esc(out, s->rec_id);
        fputs("\",\n", out);
        fputs("        \"title\": \"", out);
        wt_json_esc(out, s->title);
        fputs("\",\n", out);
        fputs("        \"command\": \"", out);
        wt_json_esc(out, s->command);
        fputs("\",\n", out);
        fputs("        \"depends_on\": ", out);
        if (s->depends_on[0] != '\0') {
            fputc('"', out);
            wt_json_esc(out, s->depends_on);
            fputc('"', out);
        } else {
            fputs("null", out);
        }
        fputs(",\n", out);
        fprintf(out, "        \"severity\": \"%s\",\n",
                wt_doctor_severity_name(s->severity));
        fprintf(out, "        \"risk\": \"%s\",\n",
                wt_doctor_risk_name(s->risk));
        fprintf(out, "        \"requires_admin\": %s,\n",
                s->requires_admin ? "true" : "false");
        fprintf(out, "        \"rollback_available\": %s,\n",
                s->rollback_available ? "true" : "false");
        fprintf(out, "        \"confidence_percent\": %d,\n",
                s->confidence_percent);
        fprintf(out, "        \"applyable\": %s,\n",
                s->applyable ? "true" : "false");
        fprintf(out, "        \"confirm_required\": %s,\n",
                s->applyable ? "true" : "false");
        fputs("        \"note\": \"", out);
        wt_json_esc(out, s->note);
        fputs("\"\n", out);
        fputs(i + 1 < plan->count ? "      },\n" : "      }\n", out);
    }
    fputs("    ]\n", out);
    fputs("  }\n", out);
    fputs("}\n", out);
}
