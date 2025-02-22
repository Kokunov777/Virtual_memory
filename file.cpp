#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <sstream>

// Константы:
const int PAGE_SIZE = 512; 
const char SIGNATURE[2] = { 'V', 'M' };


class VirtualMemory;


struct Page {
    int absoluteNumber;   
    bool modified;        
    time_t lastAccess;   
    std::vector<bool> bitmap; 
    std::vector<char> data;   

    // Конструктор:
    Page(int pageSize) : absoluteNumber(-1), modified(false), lastAccess(0), bitmap(pageSize, false), data(pageSize, 0) {}
};

// Класс, реализующий виртуальную память
class VirtualMemory {
private:
    std::string filename;    
    std::fstream file;      
    std::vector<Page> buffer;     
    long arraySize;         
    int numPages;           
    int bufferCapacity = 3;
    std::string arrayType;     
    int stringLength;      

  
    int getPageIndex(long index) const {  
        return index / (PAGE_SIZE / sizeof(char));  // Using char for byte-level addressing
    }

    //Вычисляет смещение в странице
    int getOffset(long index) const { // Changed to long
        return index % (PAGE_SIZE / sizeof(char));
    }

    //Поиск страницы в буфере
    Page* findPageInBuffer(int pageIndex) {
        for (auto& page : buffer) {
            if (page.absoluteNumber == pageIndex) {
                page.lastAccess = std::time(nullptr);
                return &page;
            }
        }
        return nullptr;
    }

    //Получение страницы из памяти или из файла
    Page* getPage(int pageIndex) {
        Page* page = findPageInBuffer(pageIndex);
        if (page) return page;

        if (buffer.size() >= bufferCapacity) {
            evictPage();
        }

        buffer.emplace_back(PAGE_SIZE); 
        Page& newPage = buffer.back(); 
        newPage.absoluteNumber = pageIndex; 
        newPage.lastAccess = std::time(nullptr);  

        file.seekg(2 + (long long)pageIndex * PAGE_SIZE, std::ios::beg); 
        if (!file.read(newPage.data.data(), PAGE_SIZE)) 
        {
            std::cerr << "Error: Could not read page from file." << std::endl;
            buffer.pop_back(); //Удаляет страницу из буфера
            return nullptr;
        }

        return &newPage; 
    }

    
    void evictPage() {
        if (buffer.empty()) return;

        auto oldestPage = std::min_element(buffer.begin(), buffer.end(), [](const Page& a, const Page& b) {
            return a.lastAccess < b.lastAccess;
            });

        if (oldestPage->modified) {
            file.seekp(2 + (long long)oldestPage->absoluteNumber * PAGE_SIZE, std::ios::beg);
            file.write(oldestPage->data.data(), PAGE_SIZE);
        }

        buffer.erase(oldestPage);
    }

    //Сброс буфера в память
    void flushBuffer() {
        for (auto& page : buffer) {
            if (page.modified) {
                file.seekp(2 + (long long)page.absoluteNumber * PAGE_SIZE, std::ios::beg);
                file.write(page.data.data(), PAGE_SIZE);
            }
        }
    }

    //Создание файла
    void createFile() {
        file.close();
        file.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            std::cerr << "Error: Could not create virtual memory file." << std::endl;
            return;
        }
        file.write(SIGNATURE, 2);
        for (int i = 0; i < numPages; ++i) {
            std::vector<char> emptyPage(PAGE_SIZE, 0);
            file.write(emptyPage.data(), PAGE_SIZE);
        }
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open virtual memory file for read/write." << std::endl;
        }
    }

public:
    // Конструктор класса:
    VirtualMemory(const std::string& fname, long size, const std::string& type, int strLen = 0)
        : filename(fname), arraySize(size), arrayType(type), stringLength(strLen) {
        numPages = (arraySize * sizeof(char) + PAGE_SIZE - 1) / PAGE_SIZE;

        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);

        if (!file.is_open()) {
            createFile();
        }
        else {
            char signatureFromFile[2];
            file.read(signatureFromFile, 2);
            if (signatureFromFile[0] != SIGNATURE[0] || signatureFromFile[1] != SIGNATURE[1]) {
                std::cerr << "Error: Invalid signature in virtual memory file. Creating a new file." << std::endl;
                file.close();
                createFile();
            }
        }
    }

    // Деструктор класса
    ~VirtualMemory() {
        flushBuffer();
        file.close();
    }

    // Метод для записи значения в элемент массива:
    bool writeValue(long index, const std::string& value) { // Changed to long
        if (index < 0 || index >= arraySize) {
            std::cerr << "Error: Index out of bounds." << std::endl;
            return false;
        }

        int pageIndex = getPageIndex(index);
        int offset = getOffset(index);
        Page* page = getPage(pageIndex);

        if (!page) {
            std::cerr << "Error: Could not retrieve page." << std::endl;
            return false;
        }

        if (arrayType == "int") {
            try {
                int intValue = std::stoi(value);
                std::memcpy(&page->data[offset], &intValue, sizeof(int));
            }
            catch (const std::invalid_argument& e) {
                std::cerr << "Error: Invalid integer value: " << value << std::endl;
                return false;
            }
            catch (const std::out_of_range& e) {
                std::cerr << "Error: Integer value out of range: " << value << std::endl;
                return false;
            }

        }
        else if (arrayType == "char") {
            if (value.length() > 0) {
                page->data[offset] = value[0];
            }
            else {
                page->data[offset] = '\0';
            }
        }
        else if (arrayType == "varchar")
        {
            size_t len = value.length();
            if (len > stringLength)
            {
                std::cerr << "Error: String length exceeds varchar limit." << std::endl;
                return false;
            }
            std::memcpy(&page->data[offset], value.c_str(), len + 1); 
        }

        page->modified = true;
        page->lastAccess = std::time(nullptr);
        page->bitmap[offset] = true; 

        return true;
    }

    // Метод для чтения значения элемента массива:
    bool readValue(long index, std::string& value) { // Changed to long
        if (index < 0 || index >= arraySize) {
            std::cerr << "Error: Index out of bounds." << std::endl;
            return false;
        }

        int pageIndex = getPageIndex(index);
        int offset = getOffset(index);

        Page* page = getPage(pageIndex);

        if (!page) {
            std::cerr << "Error: Could not retrieve page." << std::endl;
            return false;
        }


        if (arrayType == "int") {
            int intValue;
            std::memcpy(&intValue, &page->data[offset], sizeof(int));
            value = std::to_string(intValue);
        }
        else if (arrayType == "char") {
            value = page->data[offset];
        }
        else if (arrayType == "varchar")
        {
            value = &page->data[offset];
        }

        return true;
    }

    
    std::string operator[](long index) { 
        std::string value;
        if (readValue(index, value)) {
            return value;
        }
        else {
            return ""; 
        }
    }

    // Функция для вывода содержимого буфера:
    void printBufferContents() {
        std::cout << "Buffer Contents:" << std::endl;
        for (const auto& page : buffer) {
            std::cout << "  Page Number: " << page.absoluteNumber << ", Modified: " << page.modified
                << ", Last Access: " << page.lastAccess << std::endl;

            std::cout << "    Data (first 10 bytes): ";
            for (int i = 0; i < std::min<size_t>(10, page.data.size()); ++i) {
                std::cout << static_cast<int>(page.data[i]) << " ";
            }
            std::cout << std::endl;
        }
    }
};


int main() {
    VirtualMemory* vm = nullptr;

    std::string command;
    std::cout << "Create/Input/Print/Exit" << std::endl;
    std::cout << "VM> "; 

    // основа цыкла 
    while (std::getline(std::cin, command)) {
        std::stringstream ss(command); 
        std::string action;             
        ss >> action;                  


        if (action == "Create") {
            std::string filename, type; 
            long size;                    
            int stringLength = 0;         

            ss >> filename >> type; 

            // Разбираем тип, если он содержит длинy
            size_t openParenPos = type.find('('); 

            if (openParenPos != std::string::npos) {
                std::string typeName = type.substr(0, openParenPos); 
                std::string lengthStr = type.substr(openParenPos + 1, type.length() - openParenPos - 2); //Содержимое скобок

                try {
                    stringLength = std::stoi(lengthStr); 
                }
                catch (const std::invalid_argument& e) {
                    std::cerr << "Error: Invalid string length." << std::endl;
                    std::cout << "VM> ";
                    continue; 
                }

                type = typeName; 
            }

            ss >> size; 

            
            if (vm != nullptr) {
                delete vm;
            }

            // Создаем объект виртуальной памяти
            try {
                vm = new VirtualMemory(filename, size, type, stringLength);
                std::cout << "Virtual memory created." << std::endl;
            }
            catch (const std::exception& e) {
                std::cerr << "Error creating virtual memory: " << e.what() << std::endl;
            }
        }
        // Обработка команды "Input" (запись) 
        else if (action == "Input") {
            if (vm == nullptr) {
                std::cout << "Error: Create virtual memory first." << std::endl;
            }
            else {
                long index;           
                std::string value; 

                ss >> index; 

                
                std::getline(ss >> std::ws, value);

              
                if (value.length() > 1 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.length() - 2);
                }

                if (vm->writeValue(index, value)) {
                    std::cout << "Value written." << std::endl;
                }
                else {
                    std::cout << "Error writing value." << std::endl;
                }
            }
        }
        // Обработка команды "Print" (чтение)
        else if (action == "Print") {
            if (vm == nullptr) {
                std::cout << "Error: Create virtual memory first." << std::endl;
            }
            else {
                long index;         
                ss >> index; 
                std::string value; 

                if (vm->readValue(index, value)) {
                    std::cout << "Value at index " << index << ": " << value << std::endl;
                }
                else {
                    std::cout << "Error reading value." << std::endl;
                }
            }
        }
       
        else if (action == "PrintBuffer") {
            if (vm != nullptr) {
                vm->printBufferContents();
            }
            else {
                std::cout << "Error: Create virtual memory first." << std::endl;
            }

        }
       
        else if (action == "Exit") {
            break; 
        }
        
        else {
            std::cout << "Invalid command." << std::endl;
        }

        std::cout << "VM> "; 
    }

   
    if (vm != nullptr) {
        delete vm;
    }

    return 0; 
}
