#
#Regular cron jobs for the fsearch package
#
0 4 * **root[-x / usr / bin / fsearch_maintenance] &&
    / usr / bin / fsearch_maintenance
