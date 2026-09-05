from django.contrib import admin
from django.urls import path, include

urlpatterns = [
    path('admin/', admin.site.urls),
    path('api/', include('waitlist.urls')),
    path('api/', include('orders.urls')),
    path('api/auth/', include('auth_api.urls')),
    path('_allauth/', include('allauth.headless.urls')),
]
