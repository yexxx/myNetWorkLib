#include <iostream>
#include <memory>
using namespace std;

// 禁止拷贝基类
class noncopyable {
protected:
    noncopyable() {}
    ~noncopyable() {}

private:
    noncopyable(const noncopyable &that) = delete;
    noncopyable(noncopyable &&that) = delete;
    noncopyable &operator=(const noncopyable &that) = delete;
    noncopyable &operator=(noncopyable &&that) = delete;
};

class testClass : public enable_shared_from_this<testClass> {
public:
    testClass(){};

    int get() { return _num; }
    void set(int num) { _num = num; }

private:
    int _num{10};
};

int main() {
    // testClass *t{new testClass()};
    // auto tt{move(t)};

    // cout << t->get() << endl;
    // cout << tt->get() << endl;

    shared_ptr<testClass> t{make_unique<testClass>()};
    auto t1(t);

    shared_ptr<const testClass> tt(move(t1));

    cout << t.use_count() << endl;  // seg fault
    // cout << tt.use_count() << endl;
    cout << t->get() << endl;

    return 0;
}