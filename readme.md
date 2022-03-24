# AutoLightManager v 2.9.1

Модуль для автоматического управления головным светом автомобиля. Имеет три режима работы. Помещается в 168 атмегу (правда, тогда лучше без загрузчика), но на ней его работа не тестировалась ))

При запуске двигателя модуль включает головной свет (в зависимости от выбранного режима и/или времени суток), а при остановке двигателя (выключении зажигания) – отключает его. Включение света может выполняться с настраиваемой задержкой.

Модуль имеет три режима работы:
1.	«только дневные ходовые огни» – включаются только ДХО или ПТФ, ближний свет не используется и может включаться вручную;
2.	«только ближний свет» – включается только ближний свет, ДХО или ПТФ не используются и могут включаться вручную;
3.	«автоматический режим» – модуль использует встроенный датчик света и в зависимости от показаний датчика включает либо ближний свет, либо ДХО; при этом показания датчика отслеживаются непрерывно и при снижении значений до порога переключения или превышении значений выше порога переключения автоматически переключает световые приборы; порог переключения может настраиваться;

Так же имеется возможность отключения управления головным светом, в этом случае управление световыми приборами автомобиля осуществляется в ручном режиме штатными переключателями.

Дополнительно модуль имеет семисегментный четырехсимвольный экран на драйвере TM1637, который используется для настройки параметров модуля, а в остальное время отображает текущее время. Часы построены на модуле DS3231. После отключения зажигания текущее время продолжает отображаться еще 10 минут, после чего модуль уходит в спящий режим для экономии заряда аккумулятора автомобиля. Время ухода в спящий режим может настраиваться в интервале от 1 до 60 минут. Выход из спящего режима происходит при включении зажигания. При этом, если включена соответствующая настройка, на несколько секунд включается отображение текущей температуры.

Кроме экрана модуль имеет три светодиодных индикатора для отображения текущего режима работы и состояния головного света автомобиля. Для индикации используются адресные светодиоды WS2812B.

Цвета индикаторов имеют следующие значения:
1.	Красный – режим отключен; если отключены все режимы, управление головным светом выполняется вручную штатными переключателями;
2.	Зеленый – режим включен, двигатель не заведен, свет не горит;
3.	Желтый (оранжевый) – режим включен, двигатель заведен, включены ДХО или ПТФ;
4.	Голубой – режим включен, двигатель заведен, включен ближний свет;

Цвета для индикации работы ближнего света и ПТФ (ДХО) могут настраиваться в некоторых пределах.

Более подробно о работе с модулем см. в файле **docs/manual.pdf**

Принципиальную схему модуля см. в файле **docs/Schematic_Auto_light.zip**

Пины для подключения экрана, кнопок, светодиодов, входных сигналов и датчика света определены в файле **autoLightManager.h**

Модуль DS3231 подключен к пинам A4 (SDA) и A5 (SCL)

Если возникнут вопросы, пишите на valesh-soft@yandex.ru 