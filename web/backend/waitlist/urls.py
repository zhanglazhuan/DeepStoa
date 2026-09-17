from django.urls import path
from . import views

urlpatterns = [
    path('waitlist/', views.WaitlistCreateView.as_view(), name='waitlist-create'),
]
