#include <condition_variable>
#include <mutex>
class Semaphore {
public:
  Semaphore(int counter) : counter{counter} {}
  void wait() {
    std::unique_lock<std::mutex> lk{lock};
    cv.wait(lk, [this] { return this->counter > 0; });
    counter--;
  }

  void signal() {
    std::unique_lock<std::mutex> lk{lock};
    counter++;
    if (counter == 1)
      cv.notify_all();
  }

private:
  int counter;
  std::mutex lock;
  std::condition_variable_any cv;
};
