// pch.h: это предварительно скомпилированный заголовочный файл.
// Перечисленные ниже файлы компилируются только один раз, что ускоряет последующие сборки.
// Это также влияет на работу IntelliSense, включая многие функции просмотра и завершения кода.
// Однако изменение любого из приведенных здесь файлов между операциями сборки приведет к повторной компиляции всех(!) этих файлов.
// Не добавляйте сюда файлы, которые планируете часто изменять, так как в этом случае выигрыша в производительности не будет.

#ifndef PCH_H
#define PCH_H

// Добавьте сюда заголовочные файлы для предварительной компиляции
#include "framework.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <future>
#include <chrono>
#include <atomic>
#include <memory>

#include "signalrclient/hub_connection.h"
#include "signalrclient/hub_connection_builder.h"
#include "signalrclient/signalr_client_config.h"
#include "signalrclient/web_exception.h"
#include "signalrclient/signalr_value.h"

#endif //PCH_H
