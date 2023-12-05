#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <fstream>


#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif



// Глобальні змінні для прогресу
std::mutex progressMutex;
int64_t processedPixels = 0;
int64_t totalPixels = 0;


typedef HANDLE ThreadHandle;


struct ThreadData {
    unsigned char* data;
    int threadId;
    int totalThreads;
    int width;
    int height;
    int channels;
};



void printProgress() {

    int lastProgress = 0;
    while (true) {
        int currentProgress;
        {
            std::lock_guard<std::mutex> lock(progressMutex);

            currentProgress = ((processedPixels * 100) / totalPixels);

        }

        if (currentProgress != lastProgress) {
            std::cout << "Progress: " << currentProgress << "%" << std::endl;
            lastProgress = currentProgress;
        }


        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}



DWORD WINAPI ThreadFunction(LPVOID param) {

    ThreadData& data = *(ThreadData*)param;

    for (int i = data.threadId; i < data.width * data.height; i += data.totalThreads) {
        int index = i * data.channels;
        unsigned char gray = (data.data[index] + data.data[index + 1] + data.data[index + 2]) / 3;

        data.data[index] = gray;
        data.data[index + 1] = gray;
        data.data[index + 2] = gray;

        // Оновлюємо прогрес
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            processedPixels++;
        }
    }
#ifdef _WIN32
    return 0;
#else
    return nullptr;
#endif
}



void setThreadPriority(HANDLE thread, int priority) {
    SetThreadPriority(thread, priority);
}


void parallelConversion(unsigned char* data, int width, int height, int channels, int numThreads, int priority) {

    std::vector<ThreadData> threadData(numThreads);

    for (int i = 0; i < numThreads; ++i) {
        threadData[i].data = data;
        threadData[i].width = width;
        threadData[i].height = height;
        threadData[i].channels = channels;
        threadData[i].threadId = i;
        threadData[i].totalThreads = numThreads;
    }

    std::vector<ThreadHandle> threads(numThreads);

    for (int i = 0; i < numThreads; ++i) {
        threads[i] = CreateThread(nullptr, 0, ThreadFunction, &threadData[i], 0, NULL);

        setThreadPriority(threads[i], priority);
    }

    for (int i = 0; i < numThreads; ++i)
        WaitForSingleObject(threads[i], INFINITE);

}

// Функція для створення відображення файлу та зчитування зображення
#ifdef _WIN32
unsigned char* load_image_mapped(const std::wstring& filename, int* width, int* height, int* channels) {
    HANDLE fileHandle = CreateFileW(filename.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Не вдалося відкрити файл: " << filename << std::endl;
        return nullptr;
    }

    HANDLE mapFileHandle = CreateFileMapping(fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (mapFileHandle == NULL) {
        std::wcerr << L"Не вдалося створити відображення файлу" << std::endl;
        CloseHandle(fileHandle);
        return nullptr;
    }

    LPVOID mapView = MapViewOfFile(mapFileHandle, FILE_MAP_READ, 0, 0, 0);
    if (mapView == NULL) {
        std::wcerr << L"Не вдалося відобразити файл в оперативну пам'ять" << std::endl;
        CloseHandle(mapFileHandle);
        CloseHandle(fileHandle);
        return nullptr;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(fileHandle, &fileSize)) {
        std::wcerr << L"Не вдалося отримати розмір файлу: " << filename << std::endl;
        UnmapViewOfFile(mapView);
        CloseHandle(mapFileHandle);
        CloseHandle(fileHandle);
        return nullptr;
    }

    // Завантаження зображення з відображення
    unsigned char* imgData = stbi_load_from_memory(static_cast<const stbi_uc*>(mapView), static_cast<int>(fileSize.QuadPart), width, height, channels, 0); // Тут треба знати розмір файлу

    // Закриття відображення файлу
    UnmapViewOfFile(mapView);
    CloseHandle(mapFileHandle);
    CloseHandle(fileHandle);

    return imgData;
}

#else
unsigned char* load_image_mapped(const std::string& filename, int* width, int* height, int* channels) {
    // Відкриття файлу на Linux
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Не вдалося відкрити файл: " << filename << std::endl;
        return nullptr;
    }

    // Отримання розміру файлу
    struct stat fileInfo;
    if (fstat(fd, &fileInfo) == -1) {
        std::cerr << "Не вдалося отримати розмір файлу: " << filename << std::endl;
        close(fd);
        return nullptr;
    }

    // Відображення файлу в пам'ять
    void* mapView = mmap(0, fileInfo.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapView == MAP_FAILED) {
        std::cerr << "Не вдалося відобразити файл в оперативну пам'ять" << std::endl;
        close(fd);
        return nullptr;
    }

    unsigned char* imgData = stbi_load_from_memory(static_cast<const stbi_uc*>(mapView), fileInfo.st_size, width, height, channels, 0);

    // Закриття відображення файлу
    munmap(mapView, fileInfo.st_size);
    close(fd);

    return imgData;
}
#endif


void write_image_mapped(const std::wstring& filename, unsigned char* data, int dataSize) {
    // Відкриваємо файл для запису
    HANDLE fileHandle = CreateFileW(filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Не вдалося відкрити файл для запису: " << filename << std::endl;
        return;
    }

    // Створення відображення файлу
    HANDLE mapFileHandle = CreateFileMapping(fileHandle, NULL, PAGE_READWRITE, 0, dataSize, NULL);
    if (mapFileHandle == NULL) {
        std::wcerr << L"Не вдалося створити відображення файлу" << std::endl;
        CloseHandle(fileHandle);
        return;
    }

    // Відображення файлу в пам'ять
    LPVOID mapView = MapViewOfFile(mapFileHandle, FILE_MAP_WRITE, 0, 0, dataSize);
    if (mapView == NULL) {
        std::wcerr << L"Не вдалося відобразити файл в оперативну пам'ять" << std::endl;
        CloseHandle(mapFileHandle);
        CloseHandle(fileHandle);
        return;
    }

    // Копіювання даних в відображення
    memcpy(mapView, data, dataSize);

    // Очищення ресурсів
    UnmapViewOfFile(mapView);
    CloseHandle(mapFileHandle);
    CloseHandle(fileHandle);

}



std::vector<unsigned char> imageBuffer;

void my_stbi_write_func(void* context, void* data, int size) {
    // Кастуємо data до unsigned char*
    unsigned char* src = (unsigned char*)data;

    // Додаємо дані до нашого буфера
    imageBuffer.insert(imageBuffer.end(), src, src + size);
}



int main() {
    int width, height, channels;

    try {
        LARGE_INTEGER startTime, endTime, frequency;
        QueryPerformanceFrequency(&frequency);

#ifdef _WIN32
        std::wstring imagePath = L"C:\\Home\\Ivanka\\water.jpg";
#else
        std::string imagePath = "C:/Home/Ivanka/water.jpg";
#endif
        unsigned char* imgData = load_image_mapped(imagePath, &width, &height, &channels);
        if (imgData == nullptr) {
            std::cerr << "Error loading image: " << stbi_failure_reason() << std::endl;
            return 1;
        }


        totalPixels = height * width;
        std::thread progressThread(&printProgress); // викликаємо показ прогресу

        auto* originalData = new unsigned char[width * height * channels];
        std::memcpy(originalData, imgData, width * height * channels);



        std::vector<int> priorities;

        priorities = { THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_HIGHEST };


        for (int priority : priorities) {
            for (int numThreads : {2, 4, 8, 20, 100, 1000}) {
                auto* copyData = new unsigned char[width * height * channels];
                std::memcpy(copyData, originalData, width * height * channels);

                // Reset processedRows to 0 here
                {
                    std::lock_guard<std::mutex> lock(progressMutex);
                    processedPixels = 0;
                }

                std::cout << "\nTesting with " << numThreads << " threads and priority ";

                std::cout << priority;

                std::cout << std::endl;

                QueryPerformanceCounter(&startTime);

                parallelConversion(copyData, width, height, channels, numThreads, priority);

                QueryPerformanceCounter(&endTime);


                // Очищення попереднього вмісту буфера
                imageBuffer.clear();

                // Створення зображення в буфері
                stbi_write_jpg_to_func(my_stbi_write_func, nullptr, width, height, channels, copyData, 20);

                // Отримання розміру буфера
                int outputSize = static_cast<int>(imageBuffer.size());

                // Тимчасове збереження зображення для перевірки
                std::wstring tempFilename = L"temp_output_priority_" + std::to_wstring(priority) + L"_" + std::to_wstring(numThreads) + L"threads.jpg";
                std::ofstream tempFile(tempFilename, std::ios::binary);
                tempFile.write(reinterpret_cast<char*>(imageBuffer.data()), outputSize);
                tempFile.close();


                std::wstring outputFilename = L"water_output_priority_" + std::to_wstring(priority) + L"_" + std::to_wstring(numThreads) + L"threads.jpg";

                // stbi write to memory with copyData щоб зробило в формат jpg, ця функція і скаже розмір
                // і тепер замість stbi write jpt викликати мою функцію write_image_mapped

                write_image_mapped(outputFilename, imageBuffer.data(), outputSize);
                //stbi_write_jpg(filename.c_str(), width, height, channels, copyData, 100);

                double elapsedTimeParallel = static_cast<double>(endTime.QuadPart - startTime.QuadPart) / static_cast<double>(frequency.QuadPart);

                std::cout << "Conversion time with " << numThreads << " threads: " << elapsedTimeParallel << " seconds." << std::endl;


                delete[] copyData;

            }
        }

        progressThread.join(); // Ensure this is called after all threads are done

        std::cout << "Image processing completed." << std::endl;
        stbi_image_free(imgData);
        delete[] originalData;
    }
    catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
    }

    return 0;
}
