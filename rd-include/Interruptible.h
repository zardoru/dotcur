#pragma once

class InterruptedException : public std::exception
{
public:
    InterruptedException() : std::exception() {};
};

class Interruptible
{
    std::atomic<bool> InterruptRequested;
    Interruptible* Parent;
public:
    Interruptible();
    Interruptible(Interruptible* parent);
    void RequestInterrupt();
    bool IsInterruptRequested();
    void CheckInterruption();
    virtual ~Interruptible() = default;
};