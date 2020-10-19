#include <iostream>
#include <iomanip>
#include <fstream>
#include <omp.h>
#include <nlohmann/json.hpp>
#include <utility>
#include <ctime>

using namespace std;
using json = nlohmann::json;

const int DATA_NUMBER = 30;     //How much moto in one file
const int BUFFER_SIZE = DATA_NUMBER / 2;
const int CRITERIA = 26;     //select moto whose purchase value is less
const int THREAD_COUNT = 10;

class Moto {
public:
    string Manufacturer;
    int Date;
    double Distance;

    int GetRank() const {
        time_t now = time(0);
        tm *ltm = localtime(&now);
        int date = 1900 + ltm->tm_year;
        return date - Date + (int) (Distance / 1000);
    }

    Moto(string manufacturer, int date, double distance) : Manufacturer(std::move(manufacturer)), Date(date), Distance(distance) {}

    explicit Moto(const json &json) : Manufacturer(json["manufacturer"].get<string>()), Date(json["date"].get<int>()),
                                     Distance(json["distance"].get<double>()) {}

    Moto() = default;

};

class MotoWithRank {
public:
    Moto moto;
    int Rank;

    MotoWithRank(Moto m, int rank) : moto(m), Rank(rank) {}

    MotoWithRank() = default;
};

class DataMonitor {
    Moto motos[BUFFER_SIZE];
    int count = 0;
    bool finished = false;

public:
    void Finished() { finished = true; }

    int TryPush(const Moto &moto) {
        int status = 0;
        #pragma omp critical
        {
            if (count < BUFFER_SIZE) { // buffer has space
                motos[count++] = moto;
                status = 1;
            }
        }
        return status;
    }

    tuple<int, Moto> TryPop() {
        Moto moto;
        int status = 0;
        #pragma omp critical
        {
            if (count > 0) { // buffer has data
                moto = motos[--count];
                status = 1;
            } else if (finished)
                status = -1;
        }
        return {status, moto};
    }

    void Fill(Moto (&motos)[DATA_NUMBER]) {
        for (const Moto &moto: motos)
            while (!TryPush(moto)) // forcefully push until success
                continue;
        Finished();
    }
};

class ResultMonitor {
public:
    MotoWithRank Motos[DATA_NUMBER];
    int Count = 0;

    void InsertSorted(const MotoWithRank &moto) {
        #pragma omp critical (results)
        {
            int i = Count - 1;
            while (i >= 0 && (Motos[i].Rank > moto.Rank || (Motos[i].Rank == moto.Rank && Motos[i].moto.Date < moto.moto.Date))) {
                Motos[i + 1] = Motos[i];
                i--;
            }
            Motos[i + 1] = moto;
            Count++;
        }
    }

    void WriteDataToFile(const string &file, Moto (&inputData)[DATA_NUMBER]) {
        ofstream stream(file);

        stream << string (42, '-') << endl;
        stream << "|" << setw(25) << "INPUT DATA" << setw(16) << '|' << endl;
        stream << string (42, '-') << endl;
        stream << setw(14) << left << "|Manufacturer" << "|" << right << setw(10) << "Date|" << setw(17) << "Distance|" << endl;
        stream << string (42, '-') << endl;

        for(const Moto &moto: inputData)
            stream << "|" << setw(13) << left << moto.Manufacturer << right << "|"  << setw(9) << moto.Date << "|" << setw(16) << fixed << setprecision(2) << moto.Distance << "|" << endl;
        stream << string (42, '-') << endl << endl;

        stream << string (48, '-') << endl;
        stream << "|" << setw(29) << "OUTPUT DATA" << setw(18) << '|' << endl;
        stream << string (48, '-') << endl;
        stream << setw(14) << left << "|Manufacturer" << "|" << right << setw(10) << "Date|" << setw(17) << "Distance|"  << setw(6) << "Rank|"<< endl;
        stream << string (48, '-') << endl;
        for (int i = 0; i < Count; ++i) {
            const MotoWithRank &m = Motos[i];
            stream << "|" << setw(13) << left << m.moto.Manufacturer << right << "|"  << setw(9) << m.moto.Date << "|" << setw(17) << fixed << setprecision(2) << m.moto.Distance << "|" << setw(4) << m.Rank << "|" << endl;
        }
        stream << string (48, '-') << endl;

        stream.close();
    }

};

void ReadData(const string &file, Moto (&motos)[DATA_NUMBER]) {
    int i = 0;
    ifstream stream(file);
    auto json = json::parse(stream);
    for (const auto &j: json) {
        motos[i] = Moto(j);
        i++;
    }
    stream.close();
}

void StartWorker(DataMonitor &dataMonitor, ResultMonitor &resultMonitor) {
    while (true) {
        auto[status, moto] = dataMonitor.TryPop();

        if (status == 1) {
            int rank = moto.GetRank();
            if (rank < CRITERIA)
                resultMonitor.InsertSorted(MotoWithRank(moto, rank));

        } else if (status == -1)
            break;
    }
}

int main() {
    DataMonitor dataMonitor;
    ResultMonitor resultMonitor;
    Moto motos[DATA_NUMBER];
    ReadData("../../IFF-8-8_MaceinaA_L1_dat_1.json", motos);

    #pragma omp parallel num_threads(THREAD_COUNT) shared(motos, dataMonitor, resultMonitor) default(none)
    {
    #pragma omp task shared(dataMonitor, resultMonitor) default(none)
        StartWorker(dataMonitor, resultMonitor);

    #pragma omp master
        dataMonitor.Fill(motos);
    }

    resultMonitor.WriteDataToFile("../../IFF-8-8_MaceinaA_L1_rez.txt", motos);
    return 0;
}
