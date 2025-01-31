GPSd - derivative C client for logging U-Blox data in PostgreSQL

Частичный форк одного из клиентов GPSd на языке C, записывающий поступающие с USB U-Blox пространсвенные данные в PostgreSQL

## Описание программы pgubxgpsmon

### Предназначение
Приёмник спутниковых навигационных сигналов U-Blox ежесекундно присылает по USB кабелю сигналы о результатах вычисления положения своей принимающей антенны в пространстве. Эти данные записываются в группу таблиц БД Постгрес для последующей ручной оценки. К данным приписывается название серии измерений. Предполагается использование программы для размещения приёмника на измереямой точке, то есть заложенные в GPSd алгоритмы сглаживания траектории не нужны.

### Описание реализации
Программа написана на языке C путём переделки исходного кода программы gpsmon из комплекта программ GPSd. Путём исключения панельного консольного интерфейса gpsmon достигнут результат превращения вывода программы в простой протокол поступающих сигналов, включая вывод на консоль SQL запросов, связанные с состоянием успешно определившего своё полождение приёмника U-Blox. Также из полного комплекта поставки gpsmon удалены драйверы декодирования всех иных сообщений, кроме U-Blox. Предпосылки компиляции программы: пакеты libgps-dev libpq-dev.

### Описание таблиц в БД
```SQL
CREATE TABLE "Измерения"."U-Blox" (
	"Серия" varchar(80) not NULL, -- Название серии измерений
	φ float8 NULL, -- Широта
	λ float8 NULL, -- Долгота
	h float8 NULL, -- Высота над эллипсоидом
	epx float8 NULL,
	epz float8 NULL,
	evx float8 NULL,
	evy float8 NULL,
	evz float8 NULL,
	v float8 NULL, -- Скорость?
	clm float8 NULL, -- Карабкание?
	"День недели" int2 NULL, -- Индекс дня недели
	"UTC" time NULL, -- Международное координированное время в момент измерения
	epx1 float8 NULL,
	epv float8 NULL,
	"Спутников" int2 NULL, -- Количество спутников, имевших допустимый сигнал при измерении
	"Режим" int2 NULL, -- Режим достаточности определения координат
	dop float8 NULL,
	flg varchar(8) NULL -- Условные флаги наличия данных и пр.
);


COMMENT ON COLUMN "Измерения"."U-Blox"."Серия" IS 'Название серии измерений';
COMMENT ON COLUMN "Измерения"."U-Blox".φ IS 'Широта';
COMMENT ON COLUMN "Измерения"."U-Blox".λ IS 'Долгота';
COMMENT ON COLUMN "Измерения"."U-Blox".h IS 'Высота над эллипсоидом';
COMMENT ON COLUMN "Измерения"."U-Blox".v IS 'Скорость?';
COMMENT ON COLUMN "Измерения"."U-Blox".clm IS 'Карабкание?';
COMMENT ON COLUMN "Измерения"."U-Blox"."День недели" IS 'Индекс дня недели';
COMMENT ON COLUMN "Измерения"."U-Blox"."UTC" IS 'Международное координированное время в момент измерения';
COMMENT ON COLUMN "Измерения"."U-Blox"."Спутников" IS 'Количество спутников, имевших допустимый сигнал при измерении';
COMMENT ON COLUMN "Измерения"."U-Blox"."Режим" IS 'Режим достаточности определения координат';
COMMENT ON COLUMN "Измерения"."U-Blox".flg IS 'Условные флаги наличия данных и пр.';

CREATE TABLE "Измерения"."U-Blox-спутники" (
	"Серия" varchar(80) NULL,
	"UTC" time NULL,
	prn int2 NULL,
	az int2 NULL,
	el int2 NULL,
	ss int2 NULL,
	fl varchar(8) NULL,
	ok_sat bool NULL
);
```

## Процесс компиляции

Основан на частичном заимствовании компиляционного процесса `gpsmon`.

```sh
d=$(dirname $0);
CFALGS='-pthread -Wall -Wextra -fexcess-precision=standard -Wcast-align -Wcast-qual -Wimplicit-fallthrough -Wmissing-declarations -Wmissing-prototypes -Wno-missing-field-initializers -Wno-uninitialized -Wpointer-arith -Wreturn-type -Wstrict-prototypes -Wundef -Wvla -O2 -pthread -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -I/usr/include/postgresql';

gcc -o $d/gpsmon.o -c "$CFALGS" $d/gpsmon.c;
gcc -o $d/monitor_ubx.o -c "$CFALGS" $d/monitor_ubx.c;

gcc -o $d/pgubxgpsmon -pthread $d/gpsmon.o $d/monitor_ubx.o $d/lib/libgpsd.a $d/lib/libgps_static.a -lm -lrt -lncurses -ltinfo -lpq;
```

В таком виде  требуются объектные файлы  **libgpsd.a** и **libgps_static.a**, размещаемые по адресам $d/lib/libgpsd.a и $d/lib/libgps_static.a, процесс компиляции которых известен по gpsmon.

## Пример запуска программы
```sh
#!/bin/bash
read -p "Назовите группу измерений >" gr;
d=$(dirname $0);
$d/pgubxgpsmon -p "dbname='Геоинформационная система измерения на местности' host=localhost port=5432 connect_timeout=10" -s "$gr";
```
