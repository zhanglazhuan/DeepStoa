from django.contrib import admin
from .models import Order

@admin.register(Order)
class OrderAdmin(admin.ModelAdmin):
    list_display = ['id', 'user', 'status', 'quantity', 'created_at']
    list_filter = ['status']
    search_fields = ['user__email', 'user__first_name']
