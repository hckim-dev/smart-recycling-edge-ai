// 스마트 분리배출 키오스크 PC 대시보드 애플리케이션 진입점
#include "mainwindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.showFullScreen(); // 키오스크 전체화면 모드 (창 모드 개발 시 w.show() 활용)
    return QApplication::exec();
}
