#include "ironmountain_report.h"
#include "ironmountain_database.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
	char line[] = "C001,VOL001,,,02/01/2024,LTO,Description,CONTAINER,000001,01/02/2024,In Person,01/03/2024,Out Person\n";
	char comma_line[] = "C001,VOL002,,,,LTO,Description,CONTAINER,000002,01/02/2024,In Person, Jr.,,\n";
	char short_line[] = "C001,VOL003,,,,LTO,Description,CONTAINER,000003,01/02/2024\n";
	char key[64];
	char error[128] = {0};
	struct irm_record record;
	struct irm_record loaded;
	struct irm_record duplicates[2];
	unsigned int count = 1;
	const char *database = "/tmp/ironmountain-test.db";
	assert(irm_parse_record(line, sizeof(line), &record, error, sizeof(error)));
	/* Employee columns are parser input only; irm_record has no employee storage. */
	assert((record.flags & IRM_FLAG_IN) == 0);
	assert(record.slot == 1);
	assert(record.return_date.year == 2024 && record.return_date.month == 2 &&
		record.return_date.day == 1);
	assert(irm_record_key(&record, key, sizeof(key)));
	assert(strcmp(key, "C001-VOL001") == 0);
	assert(irm_database_load(database, &record, 1, error, sizeof(error)));
	assert(irm_database_get(database, key, &loaded));
	assert(strcmp(loaded.customer_id, "C001") == 0);
	assert((loaded.flags & IRM_FLAG_IN) == 0);
	duplicates[0] = record;
	duplicates[1] = record;
	strcpy(duplicates[1].description, "second occurrence");
	assert(irm_database_load(database, duplicates, 2, error, sizeof(error)));
	assert(irm_database_get(database, key, &loaded));
	assert(strcmp(loaded.description, "Description") == 0);
	assert(irm_parse_record(comma_line, sizeof(comma_line), &record, error, sizeof(error)));
	assert((record.flags & IRM_FLAG_IN) != 0);
	assert(!irm_parse_record(short_line, sizeof(short_line), &record, error, sizeof(error)));
	assert(irm_database_load(database, NULL, 0, error, sizeof(error)));
	assert(irm_database_count(database, &count));
	assert(count == 0);
	unlink(database);
	unlink("/tmp/ironmountain-test.db.library");
	unlink("/tmp/ironmountain-test.db.media-index");
	unlink("/tmp/ironmountain-test.db.state");
	return 0;
}
