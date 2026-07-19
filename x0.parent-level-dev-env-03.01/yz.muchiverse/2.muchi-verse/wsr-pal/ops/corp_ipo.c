/* corp_ipo - creates a BRAND NEW corp_<ticker> piece at runtime,
 * seeded with fresh capital, matching real WSR's own IPO/startup
 * mechanic (minimum seed capital, "season it" - SOCIETY-ECONOMY-
 * ARCHITECTURE.txt §5.5). This is the FIRST "player/AI creates a new
 * economic entity during play" mechanic in this whole family - every
 * other entity type so far (creatures, corps, governments) has been
 * pre-seeded from real data or hand-authored, never spawned live.
 * Worth getting this shape right as the reference for any future
 * runtime-entity-creation need (spin-offs, new banks, etc.).
 *
 * SIMPLIFICATIONS, named honestly: IPO stock price is a fixed $10.00
 * starting price (real WSR's own IPO pricing is presumably more
 * involved - "float a bond issue or IPO via investment bankers" per
 * the manual, not detailed further there); shares_outstanding is
 * computed backward from seed_capital/stock_price so book_value,
 * market_cap, and cash all start numerically consistent with each
 * other, not independently modeled; risk_bias starts neutral (50) -
 * there's no real weights.txt data to seed a brand-new company from,
 * unlike the 50 migrated real corporations.
 *
 * Extended 2026-07-16 to accept the real incorporation.c-style optional
 * name/industry/country fields, per direct user feedback that Startup
 * New Corp should replicate the real 5-step prompt flow (industry
 * choice/country choice/funding amount/corp name/ticker) exactly
 * instead of the fixed-value shortcut this originally was - see
 * `wsr_wizard_input.c` for the interactive prompt sequence that now
 * calls this with real user-entered values.
 *
 * Self-contained, no shared headers.
 * Usage: corp_ipo.+x <new_ticker> <seed_capital_millions> [corp_name] [industry_name] [country_name] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PATH 4096
#define PATH_BUF (MAX_PATH + 256)

static char project_root[MAX_PATH] = ".";
static const float IPO_STOCK_PRICE = 10.00f;
static const float MIN_SEED_CAPITAL = 100.0f; /* real WSR's own $100M minimum, same "millions" unit as the migrated real corp data */

static void resolve_root(void) {
    const char *env = getenv("PRISC_PROJECT_ROOT");
    if (env && env[0]) snprintf(project_root, sizeof(project_root), "%s", env);
}

int main(int argc, char *argv[]) {
    resolve_root();
    if (argc < 3) { fprintf(stderr, "Usage: corp_ipo.+x <new_ticker> <seed_capital_millions> [corp_name] [industry_name] [country_name]\n"); return 1; }
    const char *ticker = argv[1];
    float seed_capital = atof(argv[2]);
    const char *corp_name = (argc >= 4) ? argv[3] : "";
    const char *industry_name = (argc >= 5) ? argv[4] : "";
    const char *country_name = (argc >= 6) ? argv[5] : "";

    if (seed_capital < MIN_SEED_CAPITAL) {
        fprintf(stderr, "seed_capital must be at least %.2f (real WSR's own IPO minimum)\n", MIN_SEED_CAPITAL);
        return 1;
    }

    char piece_dir[PATH_BUF];
    snprintf(piece_dir, sizeof(piece_dir), "%s/projects/wsr-pal/pieces/corp_%s", project_root, ticker);

    struct stat st;
    if (stat(piece_dir, &st) == 0) {
        fprintf(stderr, "IPO failed: corp_%s already exists\n", ticker);
        return 1;
    }

    if (mkdir(piece_dir, 0755) != 0) {
        fprintf(stderr, "IPO failed: could not create %s\n", piece_dir);
        return 1;
    }

    float shares_outstanding = seed_capital / IPO_STOCK_PRICE;

    char state_path[PATH_BUF];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(state_path, sizeof(state_path), "%s/state.txt", piece_dir);
#pragma GCC diagnostic pop
    FILE *f = fopen(state_path, "w");
    if (!f) { fprintf(stderr, "IPO failed: could not write %s\n", state_path); return 1; }
    fprintf(f, "current_state=0\n");
    fprintf(f, "decision_mode=1\n");
    fprintf(f, "cash=%.2f\n", seed_capital);
    fprintf(f, "stock_price=%.2f\n", IPO_STOCK_PRICE);
    fprintf(f, "book_value=%.2f\n", seed_capital);
    fprintf(f, "shares_outstanding=%.2f\n", shares_outstanding);
    fprintf(f, "market_cap=%.2f\n", seed_capital);
    fprintf(f, "debt_to_equity=0.00\n");
    fprintf(f, "risk_bias=50\n");
    fprintf(f, "shares_held=0\n");
    fprintf(f, "pending_action=\n");
    fprintf(f, "last_action=\n");
    fprintf(f, "human_decision=\n");
    fprintf(f, "owned_by=\n");
    if (corp_name[0]) fprintf(f, "name=%s\n", corp_name);
    if (industry_name[0]) fprintf(f, "industry=%s\n", industry_name);
    if (country_name[0]) fprintf(f, "country=%s\n", country_name);
    fclose(f);

    char menu_state[PATH_BUF];
    snprintf(menu_state, sizeof(menu_state), "%s/projects/wsr-pal/pieces/wsr_menu/state.txt", project_root);
    char cmd[PATH_BUF * 2];
    snprintf(cmd, sizeof(cmd), "ls '%s/projects/wsr-pal/pieces' 2>/dev/null | grep -c '^corp_'", project_root);
    FILE *p = popen(cmd, "r");
    if (p) {
        int corp_count = 0;
        if (fscanf(p, "%d", &corp_count) == 1) {
            FILE *mf = fopen(menu_state, "r");
            char lines[64][512];
            int nlines = 0;
            if (mf) {
                while (nlines < 64 && fgets(lines[nlines], sizeof(lines[nlines]), mf)) nlines++;
                fclose(mf);
            }
            mf = fopen(menu_state, "w");
            if (mf) {
                int found_idx = 0;
                for (int i = 0; i < nlines; i++) {
                    if (strncmp(lines[i], "active_corp_index=", 18) == 0) {
                        fprintf(mf, "active_corp_index=%d\n", corp_count - 1);
                        found_idx = 1;
                    } else {
                        fputs(lines[i], mf);
                    }
                }
                if (!found_idx) fprintf(mf, "active_corp_index=%d\n", corp_count - 1);
                fclose(mf);
            }
        }
        pclose(p);
    }

    printf("IPO successful: corp_%s created with %.2f seed capital, %.2f shares at $%.2f/share\n",
           ticker, seed_capital, shares_outstanding, IPO_STOCK_PRICE);
    return 0;
}
