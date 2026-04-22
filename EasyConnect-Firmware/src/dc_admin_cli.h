#pragma once

// CLI seriale del Display Controller con livelli User/Admin.
// Auth admin temporanea con timeout di inattivita'.

void dc_admin_cli_init(void);
void dc_admin_cli_service(void);
