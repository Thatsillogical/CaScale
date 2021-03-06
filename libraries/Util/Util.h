/*
    Library for getting String and Vector (native to c++) behavior into the arduino 
*/

#ifndef Util_h
#define Util_h

/*
    Needed to transfer template classes to header or compilation failure occurs with templates of the classes defined in the main arduino hardware code
*/

template <class T>
class Node {
  public:
    Node(T d) : data(d), next(NULL) {};
    Node(T d, Node<T>* n) : data(d), next(n) {};
    void link(Node* n) {
        next = n;
    }
    Node<T>* getNext() {
        return next;
    }
    T getData() {
        return data;
    }
  private:
    T data;
    Node<T>* next;
};

template <class T>
class Stack {
  public:
    Stack() : head(NULL) {};
    void push (Node<T>* n) {
        n->link(head);
        head = n;
    }
    Node<T>* peek() {
        return head;
    }
    Node<T>* pop() {
        Node<T>* temp = head;
        head = head->getNext();
        return temp;
    }
  private:
    Node<T>* head;
};

template <class T>
class dNode {
  public:
    dNode(T d) : data(d), next(NULL), prev(NULL) {};
    dNode(dNode<T>* p, dNode<T>* n) : data(NULL), next(n), prev(p) {};
    dNode(T d, dNode<T>* p, dNode<T>* n) : data(d), prev(p), next(n) {};
    dNode<T>* getNext() {
        return next;
    }
    dNode<T>* getPrev() {
        return prev;
    }
    T getData() {
        return data;
    }
    void newPrev(dNode<T>* p) {
        prev = p;
    }
    void newNext(dNode<T>* n) {
        next = n;
    }
  private:
    T data;
    dNode<T>* next;
    dNode<T>* prev;  
};

template <class T>
class LinkedList {
  public:
    LinkedList() : head(NULL) {
        head->newPrev(head);
        head->newNext(head);
        len = 0;
    }
    void add(T element)  {
        dNode<T> newNode = *(new dNode<T>(element));
        dNode<T>* end = head->getPrev();
        newNode.newPrev(end);
        end->newNext(&newNode);
        newNode.newNext(head);
        head->newPrev(&newNode);
        len += 1;
    }
    void add(T element, int index) {
        dNode<T>* temp = head;
        while (index >= 0) {
            temp = temp->getNext();
        }
        dNode<T> newNode = *(new dNode<T>(element));
        dNode<T>* next = temp->getNext();
        newNode.newPrev(temp);
        temp->newNext(&newNode);
        newNode.newNext(next);
        next->newPrev(&newNode);
        len += 1;
    }
    void remove(int index) {
        dNode<T>* temp = head;
        while (index >= 0) {
            temp = temp->getNext();
        }
        dNode<T>* oldPrev = temp->getPrev();
        dNode<T>* oldNext = temp->getNext();
        oldPrev->newNext(oldNext);
        oldNext->newPrev(oldPrev);
        delete temp; // Clear pointer
        temp = NULL;
        len -= 1;
    }
    T get(int index) {
        dNode<T>* temp = head;
        while (index >= 0) {
            temp = temp->getNext();
        }
        return temp->getData();
    }
    int length() {
        return len;
    }
  private:
    dNode<T>* head;
    int len;
};

class CharList {
  public:
    CharList();
    CharList(char* s, int length);
    char* toChar();
    CharList operator+ (const CharList& c);
    char get(int index);
    int length();
  private:
    LinkedList<char> characters;
};

#endif
